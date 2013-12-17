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
	close(src->filefd);
	close(src->sockfd);
	checksum_hashtable_destory(src->hashtable);
	src->hashtable = NULL;
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
	if(src->checksum_header.block_nr == 0) {
		return PREPARE_RECV_CHCKSMS_NO_CHEKSMS;
	}

	/* clear buffer */
	src->length = src->offset = 0;
	memset(src->buf,0,RZYNC_BUF_SIZE);

	/* make a hash table */
	unsigned int hash_bits = choose_hash_bits(src->checksum_header.block_nr);
	src->hashtable = checksum_hashtable_init(hash_bits);
	if(!src->hashtable) {
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

int receive_checksums(rzync_src_t *src)
{
	read(src->sockfd,src->buf,RZYNC_BUF_SIZE);
	printf(src->buf);
	return 0;
}

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
						prepare_receive_checksum_header(&src);
						src->state = SRC_REQ_SENT;
					}else if(i == SEND_SYNC_REQ_ERR) {
						goto clean_up;
					}
				}
				break;
			case SRC_REQ_SENT:
				{
					int i = receive_checksum_header(&src);
					if(i == RECV_CHCKSM_H_OK) {
						int j = prepare_receive_checksums(&src);
						if(j == PREPARE_RECV_CHCKSMS_OK) {
							/* ok, set to next stage */
							src->state = SRC_CHKSM_HEADER_RECEIVED;
						} else if(j == PREPARE_RECV_CHCKSMS_NO_CHEKSMS) {
							/* no checksums */
							prepare_send_delta(&src);
						}else {
							/* error */
							goto clean_up;
						}
					} else if(i == RECV_CHCKSM_H_ERR) {
						goto clean_up;
					}
				}
				break;
			case SRC_CHKSM_HEADER_RECEIVED:
				/* recv checksums */
			case SRC_CHKSM_ALL_RECEIVED:
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

