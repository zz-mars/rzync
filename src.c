#include "rzync.h"
#include "util.h"

int init_rzyncsrc(rzync_src_t *src,char *filename)
{
	memset(src,0,sizeof(rzync_src_t));
	int filenamelen = strlen(filename);
	if(filenamelen >= RZYNC_MAX_NAME_LENGTH) {
		fprintf(stderr,"file name too long!\n");
		return 1;
	}
	strncpy(src->filename,filename,filenamelen);
	struct stat stt;
	if(stat(filename,&stt) != 0) {
		perror("stat");
		return 1;
	}
	src->size = stt.st_size;	// total size in bytes
	src->mtime = stt.st_mtime;
	src->filefd = open(filename,O_RDONLY);
	if(src->filefd < 0) {
		perror("open file");
		return 1;
	}
	src->state = SRC_INIT;
	src->offset = 0;
	src->length = 0;

	return 0;
}

void src_cleanup(rzync_src_t *src)
{
	close(src->filefd);
	close(src->sockfd);
}

void prepare_sync_request(rzync_src_t *src)
{
	int filenamelen = strlen(src->filename);
	memset(src->buf,0,RZYNC_BUF_SIZE);
	snprintf(src->buf,RZYNC_BUF_SIZE,"#%u\n$%s\n$%llu\n$%llu\n",
			filenamelen,
			src->filename,
			src->size,
			src->mtime);
	src->length = RZYNC_FILE_INFO_SIZE;
}
#define SEND_SYNC_REQ_FAIL	1
int send_sync_request(rzync_src_t *src)
{
	int n;
retry_send_req:
	n = write(src->sockfd,
			src->buf + src->offset,
			src->length - src->offset);
	if(n <= 0) {
		if(errno == EINTR) {
			goto retry_send_req;
		}
		return SEND_SYNC_REQ_FAIL;
	}
	src->offset += n;
	if(src->offset == src->length) {
		/* update the state */
		src->state = SRC_REQ_SENT;
	}
	return 0;
}

int receive_checksums(rzync_src_t *src)
{
	read(src->sockfd,src->buf,RZYNC_BUF_SIZE);
	printf(src->buf);
	return 0;
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

	/* do all the other stuff here */
	while(1) {
		switch(src.state) {
			case SRC_INIT:
				printf("current state -- SRC_INIT\n");
				/* send request here */
				prepare_sync_request(&src);
				if(send_sync_request(&src) == SEND_SYNC_REQ_FAIL) {
					goto clean_up;
				}
				break;
			case SRC_REQ_SENT:
				printf("current state -- SRC_REQ_SENT\n");
				receive_checksums(&src);
				goto clean_up;	// for test
			case SRC_CHKSM_HEADER_RECEIVED:
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

