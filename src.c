#include "rzync.h"
#include "util.h"

enum {
	INIT_RZYNC_SRC_OK = 0,
	INIT_RZYNC_SRC_ERR
};
int init_rzyncsrc(rzync_src_t *src,char *filename)
{
	/* set filename, size, mtime */
	memset(src,0,sizeof(rzync_src_t));
	int filenamelen = strlen(filename);
	if(filenamelen >= RZYNC_MAX_NAME_LENGTH) {
		fprintf(stderr,"file name too long!\n");
		return INIT_RZYNC_SRC_ERR;
	}
	strncpy(src->filename,filename,filenamelen);
	struct stat stt;
	if(stat(filename,&stt) != 0) {
		perror("stat");
		return INIT_RZYNC_SRC_ERR;
	}
	src->size = stt.st_size;	// total size in bytes
	src->mtime = stt.st_mtime;
	/* open local file */
	src->filefd = open(filename,O_RDONLY);
	if(src->filefd < 0) {
		perror("open file");
		return INIT_RZYNC_SRC_ERR;
	}
	/* lseek to start */
	lseek(src->filefd,0,SEEK_SET);
	/* set SRC_INIT state */
	src->state = SRC_INIT;
	src->offset = 0;
	src->length = 0;
	src->hashtable = NULL;
	return INIT_RZYNC_SRC_OK;
}

void src_cleanup(rzync_src_t *src)
{
	printf("src_cleanup called................\n");
	close(src->filefd);
	close(src->sockfd);
	checksum_hashtable_destory(src->hashtable);
	src->hashtable = NULL;
	if(src->checksums) {
		free(src->checksums);
		src->checksums = NULL;
	}
}

/* prepare_sync_request : Put the sync-req to the buffer */
void prepare_send_sync_request(rzync_src_t *src)
{
	int filenamelen = strlen(src->filename);
	memset(src->buf,0,RZYNC_BUF_SIZE);
	snprintf(src->buf,RZYNC_BUF_SIZE,"#%u\n$%s\n$%llu\n$%llu\n",
			filenamelen,
			src->filename,
			src->size,
			src->mtime);
	src->length = RZYNC_FILE_INFO_SIZE;
	src->offset = 0;
}

enum {
	SEND_SRC_BUF_OK = 0,
	SEND_SRC_BUF_NOT_COMPLETE,
	SEND_SRC_BUF_ERR
};

int send_src_buf(rzync_src_t *src)
{
	int n = write(src->sockfd,
			src->buf + src->offset,
			src->length - src->offset);
	if(n < 0) {
		if(errno == EINTR) {
			/* send req at next loop
			 * DO NOT CHANGE STATE NOW */
			return SEND_SRC_BUF_NOT_COMPLETE;
		}
		return SEND_SRC_BUF_ERR;
	} else if(n == 0) {
		/* connection closed */
		return SEND_SRC_BUF_ERR;
	}
	src->offset += n;
	if(src->offset == src->length) {
		return SEND_SRC_BUF_OK;
	}
	return SEND_SRC_BUF_NOT_COMPLETE;
}

enum {
	PARSE_CHECKSUM_HEADER_OK = 0,
	PARSE_CHECKSUM_HEADER_ERR
};

int parse_checksum_header(rzync_src_t *src)
{
	char *p = src->buf;
	src->checksum_header.block_nr = str2i(&p,'$','\n');
	if(src->checksum_header.block_nr == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_HEADER_ERR;
	}
	src->checksum_header.block_sz = str2i(&p,'$','\n');
	if(src->checksum_header.block_sz == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_HEADER_ERR;
	}
	printf("checksumheader -- block_nr %u -- block_sz %u\n",
			src->checksum_header.block_nr,
			src->checksum_header.block_sz);
	return PARSE_CHECKSUM_HEADER_OK;
}

/* nr/4 upward to the first num which is exp of 2 */
inline unsigned int choose_hash_bits(unsigned int nr)
{
	unsigned int slots_nr = nr/4;
	unsigned int bits = 0;
	while((1<<bits) < slots_nr) { bits++; }
	return bits;
}

enum {
	PREPARE_RECV_CHCKSMS_OK = 0,
	PREPARE_RECV_CHCKSMS_NO_CHEKSMS,
	PREPARE_RECV_CHCKSMS_ERR
};
/* When checksum header received successfully */
int prepare_receive_checksums(rzync_src_t *src)
{
	/* set checksums already recved to 0 */
	src->checksum_recvd = 0;

	if(src->checksum_header.block_nr == 0) {
		/* When no checksum */
		return PREPARE_RECV_CHCKSMS_NO_CHEKSMS;
	}

	/* clear buffer */
	src->length = src->offset = 0;
	memset(src->buf,0,RZYNC_BUF_SIZE);

	/* make a hash table for the checksums */
	unsigned int hash_bits = choose_hash_bits(src->checksum_header.block_nr);
	printf("hash_bits -- %u hash_nr -- %u\n",hash_bits,(1<<hash_bits));
	src->hashtable = checksum_hashtable_init(hash_bits);
	if(!src->hashtable) {
		return PREPARE_RECV_CHCKSMS_ERR;
	}
	src->checksums = 
		(checksum_t*)malloc(sizeof(checksum_t)*src->checksum_header.block_nr);
	if(!src->checksums) {
		perror("malloc for checksums");
		checksum_hashtable_destory(src->hashtable);
		src->hashtable = NULL;
		return PREPARE_RECV_CHCKSMS_ERR;
	}
	return PREPARE_RECV_CHCKSMS_OK;
}

enum {
	RECV_CHCKSM_H_OK = 0,
	RECV_CHCKSM_H_NOT_COMPLETE,
	RECV_CHCKSM_H_ERR
};

int receive_checksum_header(rzync_src_t *src)
{
	int n = read(src->sockfd,
			src->buf+src->length,
			RZYNC_CHECKSUM_HEADER_SIZE-src->length);
	if(n < 0) {
		if(errno == EINTR) {
			/* recv in next loop */
			return RECV_CHCKSM_H_NOT_COMPLETE;
		}
		return RECV_CHCKSM_H_ERR;
	} else if(n == 0) {
		return RECV_CHCKSM_H_ERR;
	}
	src->length += n;
	if(src->length == RZYNC_CHECKSUM_HEADER_SIZE) {
		/* parse checksum header */
		if(parse_checksum_header(src) == PARSE_CHECKSUM_HEADER_ERR) {
			return RECV_CHCKSM_H_ERR;
		}
		return RECV_CHCKSM_H_OK;
	}
	return RECV_CHCKSM_H_NOT_COMPLETE;
}

enum {
	PARSE_CHECKSUM_OK = 0,
	PARSE_CHECKSUM_ERR
};
/* Parse checksum */
int parse_checksum(char *buf,checksum_t *chksm)
{
	char *p = buf;
	memset(chksm,0,sizeof(checksum_t));
	chksm->block_nr = str2i(&p,'$','\n');
	if(chksm->block_nr == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_ERR;
	}
	chksm->rcksm.rolling_AB.A = str2i(&p,'$','\n');
	if(chksm->rcksm.rolling_AB.A == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_ERR;
	}
	chksm->rcksm.rolling_AB.B = str2i(&p,'$','\n');
	if(chksm->rcksm.rolling_AB.B == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_ERR;
	}
	if(*p++ != '$') {
		return PARSE_CHECKSUM_ERR;
	}
	strncpy(chksm->md5,p,RZYNC_MD5_CHECK_SUM_BITS);
	return PARSE_CHECKSUM_OK;
}

inline unsigned int very_simple_hash(unsigned int num,unsigned int mod)
{
	return num % mod;
}

inline void hash_insert(checksum_hashtable_t *ht,checksum_t *chksm)
{
	unsigned int hash_pos = very_simple_hash(chksm->rcksm.rolling_checksum,ht->hash_nr);
	list_add(&chksm->hash,&ht->slots[hash_pos]);
}

#define checksumof(lh)	containerof(lh,checksum_t,hash)
#define for_each_checksum_in_slot(ht,slot_nr,lh,chksm)	\
	for(lh=ht->slots[slot_nr].next,chksm=checksumof(lh);	\
			lh!=&ht->slots[slot_nr];	\
			lh=lh->next,chksm=checksumof(lh))

//#define for_each_list_head_in_slot(ht,slot_nr,lh,chksm)	\
//	for(lh=ht->slots[slot_nr].next,chksm=checksumof(lh);lh!=&ht->slots[slot_nr];lh=lh->next)

/* only compare the rolling hash
 * checking the md5, if rolling hash matches */
checksum_t *hash_search(checksum_hashtable_t *ht,unsigned int rcksm)
{
	unsigned int hash_pos = very_simple_hash(rcksm,ht->hash_nr);
	struct list_head *lh;
	checksum_t *cksm;
	for_each_checksum_in_slot(ht,hash_pos,lh,cksm) {
		if(cksm->rcksm.rolling_checksum == rcksm) {
			return cksm;
		}
	}
	return NULL;
}

void hash_analysis(checksum_hashtable_t *ht)
{
	printf("ht -- hash_bits -- %u hash_nr -- %u\n",ht->hash_bits,ht->hash_nr);
	int i;
	for(i=0;i<ht->hash_nr;i++) {
		printf("hash[%u] -- ",i);
		struct list_head *lh;
		checksum_t *chksm;
		for_each_checksum_in_slot(ht,i,lh,chksm) {
			printf("R%uA%uB%u : ",
					chksm->rcksm.rolling_checksum,
					chksm->rcksm.rolling_AB.A,
					chksm->rcksm.rolling_AB.B);
		}
		putchar('\n');
	}
}

enum {
	RECEIVE_CHCKSMS_OK = 0,
	RECEIVE_CHCKSMS_ERR
};
#define CHKSMS_EACH_BUF_SRC	(RZYNC_BUF_SIZE/RZYNC_CHECKSUM_SIZE)
/* 1) Receive checksums into the buffer
 * 2) Parse checksums and store into hash table */
int receive_checksums(rzync_src_t *src)
{
	if(src->checksum_recvd == src->checksum_header.block_nr) {
		/* all checksums recved */
		src->state = SRC_CHKSM_ALL_RECEIVED;
		return RECEIVE_CHCKSMS_OK;
	}
	unsigned int chksm_left = src->checksum_header.block_nr - src->checksum_recvd;
	/* how many checksums to recv in this loop */
	unsigned int chksm_nr = CHKSMS_EACH_BUF_SRC<chksm_left?CHKSMS_EACH_BUF_SRC:chksm_left;
	printf("checksums left -- %u CHKSMS_EACH_BUF_SRC -- %u -- chksm_nr -- %u\n",chksm_left,CHKSMS_EACH_BUF_SRC,chksm_nr);
	unsigned int bytes_nr = chksm_nr * RZYNC_CHECKSUM_SIZE;
	/* clear buf */
	src->length = 0;
	memset(src->buf,0,RZYNC_BUF_SIZE);
	/* receive the checksums */
	while(src->length != bytes_nr) {
		int n = read(src->sockfd,src->buf+src->length,bytes_nr-src->length);
		if(n < 0) {
			if(errno == EINTR) {
				continue;
			}
			/* unrecoverable error */
			return RECEIVE_CHCKSMS_ERR;
		} else if(n == 0) {
			/* connection closed */
			return RECEIVE_CHCKSMS_ERR;
		}
		src->length += n;
	}
	/* parse these checksums */
	int i;
	char *p = src->buf;
	for(i=0;i<chksm_nr;i++) {
	//	printf("%s",p);
		unsigned int block_nr = src->checksum_recvd + i;
		/* get a checksum_t for new comer */
		checksum_t *chksm = &src->checksums[block_nr];
		/* clear it */
		memset(chksm,0,sizeof(checksum_t));
		/* parse checksums */
		if(parse_checksum(p,chksm) != PARSE_CHECKSUM_OK) {
			printf("parse checksum fail...................\n");
			return RECEIVE_CHCKSMS_ERR;
		}
		assert(chksm->block_nr == block_nr);
		/* insert chksm to hash table */
		hash_insert(src->hashtable,chksm);
		p += RZYNC_CHECKSUM_SIZE;
	}
	/* update the checksums received */
	src->checksum_recvd += chksm_nr;
	return RECEIVE_CHCKSMS_OK;
}

/* prepare_receive_checksum_header : simply clear the buffer */
inline void prepare_receive_checksum_header(rzync_src_t *src)
{
	/* update the state 
	 * prepare to receive checksum header */
	src->length = src->offset = 0;
	memset(src->buf,0,RZYNC_BUF_SIZE);
}

/* simply initialize the delta struct in the rzync_src_t */
void prepare_send_delta(rzync_src_t *src)
{
	/* bytes already read from file */
	src->src_delta.offset = 0;
	memset(&src->src_delta.chksm,0,sizeof(checksum_t));
	/* clear file buf */
	src->src_delta.buf.offset = src->src_delta.buf.length = 0;
	memset(src->src_delta.buf.buf,0,RZYNC_DETLTA_BUF_SIZE);
	/* set to start */
	lseek(src->filefd,0,SEEK_SET);
}

enum {
	PREPARE_DELTA_OK = 0,
	PREPARE_DELTA_NO_MORE_TO_SEND,
	PREPARE_DELTA_ERR
};
int prepare_delta(rzync_src_t *src)
{
	/* block size */
	unsigned int block_sz = src->checksum_header.block_sz;
	unsigned int block_nr = src->checksum_header.block_nr;
	printf("block_sz -- %u block_nr -- %u\n",block_sz,block_nr);
	char *file_buf = src->src_delta.buf.buf;
	unsigned int in_buf_not_processed = 
		src->src_delta.buf.length - src->src_delta.buf.offset;
	printf("in_buf_not_processed -- %u\n",in_buf_not_processed);
	assert(in_buf_not_processed >= 0);
	if(in_buf_not_processed >= block_sz) {
		/* enough data to process,
		 * directly go to calculate delta */
		goto calculate_delta;
	}
	/* else try to read more data from file */
	printf("need more data from file.......................\n");
	if(in_buf_not_processed > 0) {
		printf("need move data in the buffer to the very start of the buffer.............\n");
		/* If there're some data in the buffer,
		 * move them to the beginning of the buffer 
		 * Use tmp buf to avoid overlapping of the move operation */
		char *tmp_buf = (char*)malloc(in_buf_not_processed+1);
		if(!tmp_buf) {
			return PREPARE_DELTA_ERR;
		}
		/* copy to tmp buffer */
		memcpy(tmp_buf,
				file_buf+src->src_delta.buf.offset,
				in_buf_not_processed);
		/* clear file buffer */
		memset(file_buf,0,RZYNC_DETLTA_BUF_SIZE);
		/* copy back */
		memcpy(file_buf,tmp_buf,in_buf_not_processed);
		free(tmp_buf);
	}else if(in_buf_not_processed == 0){
		/* simply clear the buffer */
		printf("no data in the file buf, simply clear the buffer...............\n");
		memset(file_buf,0,RZYNC_DETLTA_BUF_SIZE);
	}
	/* re-set offset and length */
	src->src_delta.buf.length = in_buf_not_processed;
	src->src_delta.buf.offset = 0;
	/* read more from file */
	unsigned long long bytes_in_file_not_processed = 
		src->size - src->src_delta.offset;
	printf("bytes_in_file_not_processed -- %llu\n",bytes_in_file_not_processed);
	if(bytes_in_file_not_processed == 0) {
		/* no more data to read from file,
		 * calculate from the data currently in buffer */
		goto calculate_delta;
	}
	assert(bytes_in_file_not_processed > 0);
	/* bytes_in_file_not_processed > 0 */
	unsigned int buf_capcity = RZYNC_DETLTA_BUF_SIZE - src->src_delta.buf.length;
	printf("buf_capcity -- %u\n",buf_capcity);
	unsigned int to_read = 
		buf_capcity<bytes_in_file_not_processed?buf_capcity:bytes_in_file_not_processed;
	printf("to_read -- %u\n",to_read);
	int already_read = 0;
	/* lseek to the right position */
	lseek(src->filefd,src->src_delta.offset,SEEK_SET);
	while(already_read != to_read) {
		int n = read(src->filefd,
				file_buf+in_buf_not_processed+already_read,
				to_read-already_read);
		if(n < 0) {
			if(errno == EINTR) {
				continue;
			}
			/* unrecoverable error */
			return PREPARE_DELTA_ERR;
		}
		already_read += n;
	}
	/* update some state */
	src->src_delta.offset += already_read;
	src->src_delta.buf.length += already_read;
	printf("buf.length -- %u\n",src->src_delta.buf.length);
	printf("file.offset -- %llu\n",src->src_delta.offset);
calculate_delta:
	in_buf_not_processed = 
		src->src_delta.buf.length - src->src_delta.buf.offset;
	printf("currently in_buf_not_processed -- %u\n",in_buf_not_processed);
	if(in_buf_not_processed == 0) {
		/* NO MORE DATA TO PROCESS */
		printf("no more data to process....................\n");
		return PREPARE_DELTA_NO_MORE_TO_SEND;
	}
	assert(in_buf_not_processed > 0);
	/* clear socket buf */
	memset(src->buf,0,RZYNC_BUF_SIZE);
	src->length = src->offset = 0;
	if(in_buf_not_processed < block_sz) {
		printf("in_buf_not_processed < block_sz...................\n");
		/* the last block to process, no need to calculate */
		int delta_header_len =	snprintf(src->buf+src->length,
				RZYNC_BUF_SIZE-src->length,
				"$%c$%u\n",
				DELTA_NDUP,in_buf_not_processed);
		printf("delta_header -- %s",src->buf+src->length);
		memcpy(src->buf+src->length+delta_header_len,
				file_buf+src->src_delta.buf.offset,
				in_buf_not_processed);
		src->length += (delta_header_len + in_buf_not_processed);
		return PREPARE_DELTA_OK;
	}
	printf("in_buf_not_processed > block_sz.......................\n");
	/* the real challenge comes here... */
	/* the block before the matched one */
	unsigned int blk_b4_match_start = src->src_delta.buf.offset;
	unsigned int blk_b4_match_end = blk_b4_match_start;
	/* the block which is checked for match */
	unsigned int checking_match_start = blk_b4_match_end;
	unsigned int checking_match_end = checking_match_start + block_sz;
	assert(checking_match_end < src->src_delta.buf.length);
	/* calculate the rolling checksum of the first block */
	rolling_checksum_t rcksm = adler32_direct(file_buf+checking_match_start, block_sz);
	printf("first block -- rolling_checksum -- %u rolling_checksum.A -- %u rolling_checksum.B -- %u\n",
			rcksm.rolling_checksum,rcksm.rolling_AB.A,rcksm.rolling_AB.B);
	{
		/* for test */
		checksum_t *testck = hash_search(src->hashtable,rcksm.rolling_checksum);
		if(!testck) {
			printf("first blk not found!\n");
		} else {
			printf("first blk found...............\n");
		}
	}
	/* try to pack the bytes in file buffer into the socket buffer */
	while(1) {
	//	printf("b4_match_start -- %u b4_match_end -- %u -- match_start -- %u match_end -- %u\n",
	//			blk_b4_match_start,blk_b4_match_end,checking_match_start,checking_match_end);
		int match_found = 0;	// initialized to 0 as not_found
		/* try to find a matched block in the file buffer */
		checksum_t *chksm = hash_search(src->hashtable,rcksm.rolling_checksum);
		if(!chksm) {
		//	printf("rolling checksum match not found!.............................\n");
			/* rolling checksum not match */
			goto one_byte_forward;
		}else {
			/* rolling checksum matched, compare the md5 */
			printf("rolling checksum match found.............................\n");
			char md5[RZYNC_MD5_CHECK_SUM_BITS+1];
			memset(md5,0,RZYNC_MD5_CHECK_SUM_BITS+1);
			md5s_of_str(file_buf+checking_match_start,block_sz,md5);
			if(memcmp(md5,chksm->md5,RZYNC_MD5_CHECK_SUM_BITS) == 0) {
				/* matched block found */
				match_found = 1;
				goto pack_delta;
			} else {
				goto one_byte_forward;
			}
		}
one_byte_forward:
		/* is there still some bytes for us to move forward? */
		if(checking_match_end == src->src_delta.buf.length) {
			/* cannot move forward 
			 * return after pack delta into buffer */
			goto pack_delta;
		}
		assert(checking_match_end < src->src_delta.buf.length);
		unsigned char old_ch = file_buf[checking_match_start++];
		unsigned char new_ch = file_buf[++checking_match_end];
		blk_b4_match_end = checking_match_start;
		rcksm = adler32_rolling(old_ch,new_ch,block_sz,rcksm);
		continue;
pack_delta:
		/* We get here for two possible reasons:
		 * 1) A match is found, pack the unmatched(if there is one..)
		 * and the matched block into the socket buffer, update some 
		 * states and continue.
		 *
		 * 2) No match is found, the checking_match block must have 
		 * reached the end of the file buffer. We pack all the bytes 
		 * within the range of 
		 * [ src->src_delta.buf.offset , checking_match_start ]
		 * ( with both side inclusive )
		 *
		 * ---------------- about the buffer management --------------
		 *  There's no need to check if there is free space in the 
		 *  buffer, because the file buffer is of the same size with 
		 *  the socket size.
		 *  Each time prepare_delta() function is called, we assume 
		 *  that bytes in socket buffer are all sent, so we can clear
		 *  the buffer and get ready to hold the new delta data.
		 *  At the same time, the data in file buffer is at most equal 
		 *  to the size of socket buffer. However we will never send 
		 *  more than a total file_buf as delta data. Actually, the most
		 *  number of bytes we will send as delta data is
		 *
		 *		RZYNC_DETLTA_BUF_SIZE - RZYNC_BLOCK_SIZE + 1
		 *
		 *  which is the worst case when no matched block is found in 
		 *  the whole buffer.
		 * */
		if(match_found == 1) {
			/* pack the unmatched(if there is one..) and matched block */
			unsigned int un_matched_block_sz = 
				blk_b4_match_end - blk_b4_match_start;
			assert(un_matched_block_sz >= 0);
			if(un_matched_block_sz > 0) {
				/* there is an unmatched block before the matched one */
				/* pack delta header */
				int delta_header_sz = snprintf(src->buf+src->length,
						RZYNC_BUF_SIZE-src->length,
						"$%c$%u\n",
						DELTA_NDUP,un_matched_block_sz);
				/* update src->length */
				src->length += delta_header_sz;
				/* packer delta data */
				memcpy(src->buf+src->length,
						file_buf+blk_b4_match_start,
						un_matched_block_sz);
				src->length += un_matched_block_sz;
			}
			/* pack the header of duplicated block */
			int delta_header_sz = snprintf(src->buf+src->length,
					RZYNC_BUF_SIZE-src->length,
					"$%c$%u\n",
					DELTA_DUP,chksm->block_nr);
			src->length += delta_header_sz;
			/* update state */
			src->src_delta.buf.offset = checking_match_end;
			blk_b4_match_start = src->src_delta.buf.offset;
			blk_b4_match_end = blk_b4_match_start;
			checking_match_start = blk_b4_match_end;
			checking_match_end = checking_match_start + block_sz;
			if(checking_match_end > src->src_delta.buf.length) {
				/* less than one block left in the buffer */
				return PREPARE_DELTA_OK;
			}
		}else {
			/* no match found, just pack the unmatched delta
			 * after then, return PREPARE_DELTA_OK */
			src->src_delta.buf.offset = (++blk_b4_match_end);
			unsigned un_matched_block_sz = 
				blk_b4_match_end - blk_b4_match_start;
			assert(un_matched_block_sz > 0);
			int delta_header_sz = 
				snprintf(src->buf+src->length,
						RZYNC_BUF_SIZE-src->length,
						"$%c$%u\n",
						DELTA_NDUP,un_matched_block_sz);
			src->length += delta_header_sz;
			memcpy(src->buf+src->length,
					file_buf+blk_b4_match_start,
					un_matched_block_sz);
			src->length += un_matched_block_sz;
			return PREPARE_DELTA_OK;
		}
	}
	/* impossible */
	printf("Something is wrong if you see this...........................\n");
	return PREPARE_DELTA_OK;
}


int main(int argc,char *argv[])
{
	if(argc != 2) {
		fprintf(stderr,"please specify the file to be synced!\n");
		return 1;
	}

	char *filename = argv[1];
	/* ELEMENTS WHICH WILL BE SET IN INITIALIZATION
	 * @filename
	 * @size
	 * @mtime
	 * @filefd
	 * @state
	 * @offset
	 * @length
	 * RETURN 1 ON ERROR
	 * */
	rzync_src_t src;
	if(init_rzyncsrc(&src,filename)) {
		return 1;
	}
	
	/* connect the dst */
	struct sockaddr_in addr;
	int addr_len = sizeof(addr);

	memset(&addr,0,addr_len);
	addr.sin_family = AF_INET;
//	inet_pton(AF_INET,ZZIP,(void*)&addr.sin_addr);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(RZYNC_PORT);

	src.sockfd = socket(AF_INET,SOCK_STREAM,0);

	if(src.sockfd < 0) {
		perror("socket");
		return 1;
	}
	
	if(connect(src.sockfd,(struct sockaddr*)&addr,addr_len) != 0) {
		perror("connect");
		return 1;
	}

	/* prepare sync req, put sync req to the buffer */
	prepare_send_sync_request(&src);
	while(1) {
		switch(src.state) {
			case SRC_INIT:
				{
					/* send sync req in the buffer */
					int i = send_src_buf(&src);
					if(i == SEND_SRC_BUF_OK) {
						/* when sync req sent ok
						 * prepare to receive checksum header 
						 * simply by clearing the buf */
						prepare_receive_checksum_header(&src);
						/* update to new stage */
						src.state = SRC_REQ_SENT;
					}else if(i == SEND_SRC_BUF_ERR) {
						goto clean_up;
					}
					/* else if sync req is not sent completely,
					 * send it in next loop 
					 * state is not changed now */
				}
				break;
			case SRC_REQ_SENT:
				{
					int i = receive_checksum_header(&src);
					if(i == RECV_CHCKSM_H_OK) {
						/* prepare to receive the checksums */
						int j = prepare_receive_checksums(&src);
						if(j == PREPARE_RECV_CHCKSMS_OK) {
							/* ok, set to next stage 
							 * ready to receive all the checksums */
							src.state = SRC_CHKSM_HEADER_RECEIVED;
						} else if(j == PREPARE_RECV_CHCKSMS_NO_CHEKSMS) {
							/* When no checksums need to be received
							 * directly go to stage SRC_CHKSM_ALL_RECEIVED */
							src.state = SRC_CHKSM_ALL_RECEIVED;
						}else {
							/* error */
							goto clean_up;
						}
					} else if(i == RECV_CHCKSM_H_ERR) {
						goto clean_up;
					}
					/* else if checksum header receive imcompletely */
				}
				break;
			case SRC_CHKSM_HEADER_RECEIVED:
				/* recv checksums */
				if(receive_checksums(&src) == RECEIVE_CHCKSMS_ERR) {
					printf("receive checksums fail.....................\n");
					goto clean_up;
				}
				break;
			case SRC_CHKSM_ALL_RECEIVED:
				printf("All checksums have been received successfully...............\n");
				/* When all checksums recved, prepare for delta file */
				prepare_send_delta(&src);
				/* set to next stage */
				src.state = SRC_CALCULATE_DELTA;
			//	/*-------------------------------------------------------------------- test  ---------------*/
				hash_analysis(src.hashtable);
			//	goto clean_up;
			//	/*-------------------------------------------------------------------- test  ---------------*/
				break;
			case SRC_CALCULATE_DELTA:
				/* process the file in the buffer 
				 * calculate the delta data
				 * pack the result to the buffer */
				{
					int i = prepare_delta(&src);
					if(i == PREPARE_DELTA_OK) {
						printf("prepare_delta ok...................\n");
					//	printf("%s\n",src.buf);
						goto clean_up;	// for test
						/* set to SRC_SEND_DELTA */
						src.state = SRC_SEND_DELTA;
					}else if(i == PREPARE_DELTA_NO_MORE_TO_SEND) {
						/* set to SRC_DELTA_FILE_DONE 
						 * since all deltas have been sent */
						src.state = SRC_DELTA_FILE_DONE;
					}else {
						goto clean_up;
					}
				}
				break;
			case SRC_SEND_DELTA:
				/*---------------- for test  -----------------*/
				/* for test : do not send the delta */
				src.state = SRC_CALCULATE_DELTA;
				break;
				/*---------------- for test  -----------------*/
				/* send the data in the buf */
				{
					int i = send_src_buf(&src);
					if(i == SEND_SRC_BUF_OK) {
						src.state = SRC_CALCULATE_DELTA;
					} else if(i == SEND_SRC_BUF_ERR) {
						goto clean_up;
					}
				}
				break;
			case SRC_DELTA_FILE_DONE:
				/* Done */
				goto clean_up;
			case SRC_DONE:
				/* undefined */
				goto clean_up;
			default:
				goto clean_up;
		}
	}
clean_up:
	src_cleanup(&src);
	return 0;
}

