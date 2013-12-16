#include "rzync.h"
#include "util.h"

static int g_continue_flag = 1; // continue flag, default set to 1
struct event_base *ev_base = NULL;	// initialize to NULL,will be set on start
rzyncdst_freelist_t * free_list = NULL;

void rzyncdst_ins_cleanup(rzync_dst_t *ins)
{
	printf("cleanup called................\n");
	/* close file descriptor according its state */
//	close(ins->filefd);
//	close(ins->dstfd);
//	close(ins->sockfd);
	memset(ins,0,sizeof(rzync_dst_t));
	put_rzyncdst(free_list,ins);
}

enum on_read_init_rt {
	ON_READ_INIT_OK = 0,
	ON_READ_INIT_ERR_OK,
	ON_READ_INIT_ERR_NEED_CLEADUP
};
/* read the sync req header */
int on_read_init(rzync_dst_t *ins)
{
	int n = read(ins->sockfd,
			ins->buf + ins->length,
			RZYNC_FILE_INFO_SIZE - ins->length);
	if (n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* handle error */
			return ON_READ_INIT_ERR_NEED_CLEADUP;
		} else { 
			/* not error for non-blocking IO */
			return ON_READ_INIT_ERR_OK; 
		}
	} else if(n == 0) {
		/* connection closed by peer */
		return ON_READ_INIT_ERR_NEED_CLEADUP;
	}

	/* when n > 0, update ins->length */
	ins->length += n;
	if(ins->length < RZYNC_FILE_INFO_SIZE) {
		/* request header not all received 
		 * DO NOT CHANGE THE STATE NOW */
		return ON_READ_INIT_OK;
	}

	printf("from client : %s\n",ins->buf);

	/* deal with the request here */
	char *p = ins->buf;
	int filenamelen = str2i(&p,'#','\n');
	if(filenamelen == STR2I_PARSE_FAIL ||
			filenamelen > (RZYNC_MAX_NAME_LENGTH-1) ||
			*p++ != '$') {
		/* illegal request */
		return ON_READ_INIT_ERR_NEED_CLEADUP;
	}
	memset(ins->filename,0,RZYNC_MAX_NAME_LENGTH);
	strncpy(ins->filename,p,filenamelen);
	p += (filenamelen+1);
	long long fsize = str2ll(&p,'$','\n');
	if(fsize == STR2LL_PARSE_FAIL) {
		return ON_READ_INIT_ERR_NEED_CLEADUP;
	}
	ins->size = fsize;
	long long mtime = str2ll(&p,'$','\n');
	if(mtime == STR2LL_PARSE_FAIL) {
		return ON_READ_INIT_ERR_NEED_CLEADUP;
	}
	ins->mtime = mtime;
	/* request parse ok */
	printf("/* request parse ok, update to new state */\n");
	printf("filename -- %s\n",ins->filename);
	printf("size -- %llu\n",ins->size);
	printf("mtime -- %llu\n",ins->mtime);
	ins->state = DST_REQ_RECEIVED;

	return ON_READ_INIT_OK;
}

enum on_write_send_checksum_header_rt {
	ON_WRITE_SEND_CHECKSUM_HEADER_OK = 0,
	ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE,
	ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP
};
/* send checksum header and all the checksums */
int on_write_send_checksum_header(rzync_dst_t *ins)
{
	/* checksum header now in the buffer,
	 * send header first */
	int n = write(ins->sockfd,ins->buf+ins->offset,ins->length-ins->offset);
	if (n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* handle error */
			return ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP;
		} else { 
			return ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE; 
		}
	} else if(n == 0) {
		/* connection closed by peer,
		 * need cleanup */
		return ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP;
	}
	ins->offset += n;
	if(ins->offset == ins->length) {
		/* set to new stage 
		 * ready to send the checksums */
		ins->state = DST_CHKSM_HEADER_SENT;
	}
	return ON_WRITE_SEND_CHECKSUM_HEADER_OK;
}

enum {
	PREPARE_SEND_CHECKSUMS_OK = 0,
	PREPARE_SEND_CHECKSUMS_NOT_OK
};
/* @prepare_send_checksums 
 * 1) open file
 * 2) lseek to start
 * 3) set checksum_sent to 0 */
inline int prepare_send_checksums(rzync_dst_t *ins)
{
	/* open dst_local_file and set checksum_sent to 0 */
	ins->dst_local_file.fd = open(ins->filename,O_RDONLY);
	if(ins->dst_local_file.fd < 0) {
		perror("open dst_local_file");
		return PREPARE_SEND_CHECKSUMS_NOT_OK;
	}
	lseek(ins->dst_local_file.fd,0,SEEK_SET);
	ins->dst_local_file.checksum_sent = 0;	// initialize to 0
	return PREPARE_SEND_CHECKSUMS_OK;
}

#define CHECKSUMS_NR_IN_TOTAL_BUF	(RZYNC_BUF_SIZE/RZYNC_CHECKSUM_SIZE)
enum {
	STILL_MORE_CHECKSUMS_TO_SEND = 0,
	LAST_CHECKSUM_NOW
};
int prepare_checksums(rzync_dst_t *ins)
{
	int rt;
	int checksums_left = ins->dst_local_file.block_nr - ins->dst_local_file.checksum_sent;
	int how_many_to_send_this_time; 
	if(CHECKSUMS_NR_IN_TOTAL_BUF < checksums_left) {
		/* most of the cases */
		how_many_to_send_this_time = CHECKSUMS_NR_IN_TOTAL_BUF;
		rt = STILL_MORE_CHECKSUMS_TO_SEND;
	}else {
		/* for the last groups of checksums */
		how_many_to_send_this_time = checksums_left;
		rt = LAST_CHECKSUM_NOW;
	}

	ins->offset = ins->length = 0;
	memset(ins->buf,0,RZYNC_BUF_SIZE);
	checksum_t chksm;
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	char filebuf[RZYNC_BLOCK_SIZE];

	int i;
	char *p = buf;
	for(i=0;i<how_many_to_send_this_time;i++) {
		memset(filebuf,0,RZYNC_BLOCK_SIZE);
		if(read(ins->dst_local_file.fd,buf,RZYNC_BLOCK_SIZE) != RZYNC_BLOCK_SIZE) {
		}
		chksm.block_nr = ins->dst_local_file.checksum_sent + i;
		chksm.rcksm = adler32_direct(filebuf,RZYNC_BLOCK_SIZE);
	}
	return rt;
}

void on_write(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t *)arg;
	assert(sock == ins->sockfd);
	switch(ins->state) {
		case DST_INIT:
			/* undefined */
			break;
		case DST_REQ_RECEIVED:
			/* send the checksum header and all the checksum here */
			{
				int i = on_write_send_checksum_header(ins);
				if(i == ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP) {
					/* goto cleanup if needed... */
					goto clean_up;
				} else if(i == ON_WRITE_SEND_CHECKSUM_HEADER_OK) {
					/* checksum header sent ok */
					if(ins->dst_local_file.block_nr == 0) {
						/* add ev_read and get ready to receive the sync data
						 * if no checksum need to be sent... */
						goto re_add_ev_read;
					}

					if(prepare_send_checksums(ins) == PREPARE_SEND_CHECKSUMS_NOT_OK) {
						goto clean_up;
					}
					/* the first time calling prepare_checksums */
					prepare_checksums(ins);
				}
				// need to re-add ev_write
				goto re_add_ev_write;
			}
			break;
		case DST_CHKSM_HEADER_SENT:
			/* send the checksums now in the buf
			 * prepare other checksums if needed */
			break;
		case DST_CHKSM_ALL_SENT:
			break;
		case DST_DELTA_FILE_RECEIVED:
		case DST_DONE:
		default:
			break;
	}
	return;
re_add_ev_read:
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"ev_read add fail!\n");
		goto clean_up;
	}
	return;
re_add_ev_write:
	if(event_add(&ins->ev_write,NULL) != 0) {
		fprintf(stderr,"ev_write add fail!\n");
		goto clean_up;
	}
	return;
clean_up:
	rzyncdst_ins_cleanup(ins);
}

enum on_read_prepare_checksum_header_rt {
	ON_READ_PREPARE_CHECKSUM_HEADER_OK = 0,
	ON_READ_PREPARE_CHECKSUM_HEADER_NEED_CLEANUP
};
/* 1) Prepare the checksum header in the buffer
 * 2) set ev_write */
int on_read_prepare_checksum_header(rzync_dst_t *ins)
{
	ins->dst_local_file.block_sz = RZYNC_BLOCK_SIZE;
	struct stat stt;
	unsigned long long file_sz = 
	(stat(ins->filename,&stt) == 0)?stt.st_size:0;
	ins->dst_local_file.block_nr = file_sz / ins->dst_local_file.block_sz;
	memset(ins->buf,0,RZYNC_BUF_SIZE);
	snprintf(ins->buf,
			RZYNC_CHECKSUM_HEADER_SIZE,
			"$%u\n$%u\n",
			ins->dst_local_file.block_nr,
			ins->dst_local_file.block_sz);
	ins->length = RZYNC_CHECKSUM_HEADER_SIZE;
	ins->offset = 0;

	/* checksum header now ok 
	 * set the ev_write */
	event_set(&ins->ev_write,ins->sockfd,EV_WRITE,on_write,(void*)ins);
	if(event_base_set(ev_base,&ins->ev_write) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		return ON_READ_PREPARE_CHECKSUM_HEADER_NEED_CLEANUP;
	}
	// add write event
	if(event_add(&ins->ev_write,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		return ON_READ_PREPARE_CHECKSUM_HEADER_NEED_CLEANUP;
	}
	return ON_READ_PREPARE_CHECKSUM_HEADER_OK;
}

void on_read(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t*)arg;
	assert(sock == ins->sockfd);
	switch(ins->state) {
		case DST_INIT:
			/* read RZYNC_FILE_INFO */
			{
				enum on_read_init_rt i = on_read_init(ins);
				if(i == ON_READ_INIT_ERR_NEED_CLEADUP) {
					goto clean_up;
				}
				goto re_add_ev_read;
			}
		case DST_REQ_RECEIVED:
			/* prepare checksum header */
			{
				enum on_read_prepare_checksum_header_rt rt = 
					on_read_prepare_checksum_header(ins);
				if(rt == ON_READ_PREPARE_CHECKSUM_HEADER_NEED_CLEANUP) {
					goto clean_up;
				}
			}
			/* no need to add read event now */
			return;
		case DST_CHKSM_HEADER_SENT:
			/* undefined */
			break;
		case DST_CHKSM_ALL_SENT:
			/* ready to receive delta file */
			break;
		case DST_DELTA_FILE_RECEIVED:
		case DST_DONE:
		default:
			break;
	}

re_add_ev_read:
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		goto clean_up;
	}
	return;
clean_up:
	rzyncdst_ins_cleanup(ins);
}

void on_conenct(int sock,short event,void *arg)
{
	struct sockaddr_in claddr;
	int claddrlen;

	int connfd = accept(sock,(struct sockaddr*)&claddr,&claddrlen);

	if(connfd < 0) {
		if(!(errno == EAGAIN || errno == EWOULDBLOCK)) {
			perror("accept");
		}
		// return anyway
		return;
	}

	printf("new connection comes\n");

	rzync_dst_t *ins = get_rzyncdst(free_list);
	if(!ins) {
		fprintf(stderr,"get_rzyncdst fail!\n");
		return;
	}

	memset(ins,0,sizeof(rzync_dst_t));
	ins->sockfd = connfd;
	ins->state = DST_INIT;
	ins->length = ins->offset = 0;
	
	/* set read event */
	event_set(&ins->ev_read,ins->sockfd,EV_READ,on_read,(void*)ins);
	if(event_base_set(ev_base,&ins->ev_read) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		goto clean_up;
	}
	// add read event
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		goto clean_up;
	}
	return;
clean_up:
	rzyncdst_ins_cleanup(ins);
}


int main()
{
	struct sockaddr_in addr;
	int addr_len = sizeof(addr);
	memset(&addr,0,addr_len);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(RZYNC_PORT);

	int lfd = socket(AF_INET,SOCK_STREAM,0);
	if(lfd < 0) {
		perror("socket");
		return 1;
	}

	if(bind(lfd,(struct sockaddr*)&addr,addr_len) != 0) {
		perror("bind");
		return 1;
	}

	/* set non-block */
	set_nonblock(lfd);
	listen(lfd,8);

	/* initialize free list */
	free_list = rzyncdst_freelist_init();
	if(!free_list) {
		fprintf(stderr,"initialize free list fail!\n");
		return 1;
	}

	/* set event */
	ev_base = event_base_new();
	if(!ev_base) {
		fprintf(stderr,"event_base_new fail!\n");
		return 1;
	}

	struct event e_lfd;
	event_set(&e_lfd,lfd,EV_READ | EV_PERSIST,on_conenct,NULL);
	if(event_base_set(ev_base,&e_lfd) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		return 1;
	}
	if(event_add(&e_lfd,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		return 1;
	}

	while(g_continue_flag) {
		event_base_loop(ev_base,0);
	}

	/* cleanup stuff */
	event_base_free(ev_base);
	rzyncdst_freelist_destory(free_list);

	return 0;
}

