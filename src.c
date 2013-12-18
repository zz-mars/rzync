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
	SEND_SYNC_REQ_OK = 0,
	SEND_SYNC_REQ_NOT_COMPLETE,
	SEND_SYNC_REQ_ERR
};
/* send_sync_request : Send the sync-req in the buffer */
int send_sync_request(rzync_src_t *src)
{
	int n = write(src->sockfd,
			src->buf + src->offset,
			src->length - src->offset);
	if(n < 0) {
		if(errno == EINTR) {
			/* send req at next loop
			 * DO NOT CHANGE STATE NOW */
			return SEND_SYNC_REQ_NOT_COMPLETE;
		}
		return SEND_SYNC_REQ_ERR;
	} else if(n == 0) {
		/* connection closed */
		return SEND_SYNC_REQ_ERR;
	}
	src->offset += n;
	if(src->offset == src->length) {
		return SEND_SYNC_REQ_OK;
	}
	return SEND_SYNC_REQ_NOT_COMPLETE;
}

enum {
	PARSE_CHECKSUM_HEADER_OK = 0,
	PARSE_CHECKSUM_HEADER_ERR
};

int parse_checksum_header(rzync_src_t *src)
{
	char *p = src->buf;
	printf("checksum header -- %s\n",p);
	src->checksum_header.block_nr = str2i(&p,'$','\n');
	if(src->checksum_header.block_nr == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_HEADER_ERR;
	}
	src->checksum_header.block_sz = str2i(&p,'$','\n');
	if(src->checksum_header.block_sz == STR2I_PARSE_FAIL) {
		return PARSE_CHECKSUM_HEADER_ERR;
	}
	printf("block_nr %u\nblock_sz %u\n",
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
	src->hashtable = checksum_hashtable_init(hash_bits);
	if(!src->hashtable) {
		return PREPARE_RECV_CHCKSMS_ERR;
	}
	src->checksums = (checksum_t*)malloc(sizeof(checksum_t)*src->checksum_header.block_nr);
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

inline void hash_insert(checksum_hashtable_t *ht,checksum_t *chksm)
{
	int hash_pos = chksm->rcksm.rolling_checksum % ht->hash_nr;
	list_add(&chksm->hash,&ht->slots[hash_pos]);
}

#define checksumof(lh)	containerof(lh,checksum_t,hash)
#define for_each_checksum_in_slot(ht,slot_nr,lh,chksm)	\
	for(lh=&ht->slots[slot_nr].next,chksm=checksumof(lh);	\
			lh!=&ht->slots[slot_nr];	\
			lh=lh->next)
/* only compare the rolling hash
 * checking the md5, if rolling hash matches */
checksum_t *hash_search(checksum_hashtable_t *ht,checksum_t *chksm)
{
	int hash_pos = chksm->rcksm.rolling_checksum % ht->hash_nr;
	struct list_head *lh;
	checksum_t *cksm;
	for_each_checksum_in_slot(ht,hash_pos,lh,cksm) {
		if(cksm->rcksm.rolling_checksum == chksm->rcksm.rolling_checksum) {
			return cksm;
		}
	}
	return NULL;
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
//	printf("checksums -- %s\n",src->buf);
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
					/* send sync req */
					int i = send_sync_request(&src);
					if(i == SEND_SYNC_REQ_OK) {
						/* when sync req sent ok
						 * prepare to receive checksum header */
						prepare_receive_checksum_header(&src);
						src.state = SRC_REQ_SENT;
					}else if(i == SEND_SYNC_REQ_ERR) {
						goto clean_up;
					}
					/* else if sync req is not sent completely,
					 * send it in next loop */
				}
				break;
			case SRC_REQ_SENT:
				{
					int i = receive_checksum_header(&src);
					if(i == RECV_CHCKSM_H_OK) {
						int j = prepare_receive_checksums(&src);
						if(j == PREPARE_RECV_CHCKSMS_OK) {
							/* ok, set to next stage */
							src.state = SRC_CHKSM_HEADER_RECEIVED;
						} else if(j == PREPARE_RECV_CHCKSMS_NO_CHEKSMS) {
							/* no checksums */
							src.state = SRC_CHKSM_ALL_RECEIVED;
						//	prepare_send_delta(&src);
						//	goto clean_up;	// for test
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
				/* When all checksums recved,
				 * prepare for delta file */
				printf("All checksums have been received successfully...............\n");
//				prepare_send_delta(&src);
				goto clean_up;
			case SRC_CALCULATING_DELTA:
			case SRC_DELTA_FILE_DONE:
			case SRC_DONE:
			default:
				break;
		}
	}
clean_up:
	src_cleanup(&src);
	return 0;
}

