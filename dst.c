#include "rzync.h"
#include "util.h"

static int g_continue_flag = 1; // continue flag, default set to 1
struct event_base *ev_base = NULL;	// initialize to NULL,will be set on start
rzyncdst_freelist_t * free_list = NULL;

void rzyncdst_ins_cleanup(rzync_dst_t *ins)
{
	printf("cleanup called................\n");
	close(ins->sockfd);
	/* close file descriptor according its state */
	if(ins->state >= DST_CHKSM_HEADER_SENT) {
		close(ins->dst_local_file.fd);
	}
	if(ins->state >= DST_CHKSM_ALL_SENT) {
		close(ins->dst_sync_file.fd);
	}
	memset(ins,0,sizeof(rzync_dst_t));
	put_rzyncdst(free_list,ins);
}

enum on_read_init_rt {
	ON_READ_INIT_COMPLETE = 0,
	ON_READ_INIT_NOT_COMPLETE,
	ON_READ_INIT_ERR_NEED_CLEADUP
};
/* on_read_init : read the sync req header
 * return : ON_READ_INIT_COMPLETE if req received successfully
 *			ON_READ_INIT_NOT_COMPLETE if more bytes need to be read
 *			ON_READ_INIT_ERR_NEED_CLEADUP if some errors happened.
 *	*/
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
			return ON_READ_INIT_NOT_COMPLETE; 
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
		return ON_READ_INIT_NOT_COMPLETE;
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
	/* update the state */
	ins->state = DST_REQ_RECEIVED;

	return ON_READ_INIT_COMPLETE;
}

enum {
	ON_WRITE_SEND_CHECKSUM_HEADER_OK = 0,
	ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE,
	ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP
};
/* send checksum header
 * IF SEND OK, UPDATE THE STATE TO DST_CHKSM_HEADER_SENT */
int on_write_send_checksum_header(rzync_dst_t *ins)
{
	/* checksum header now in the buffer,
	 * send header first */
	int n = write(ins->sockfd,ins->buf+ins->offset,ins->length-ins->offset);
	if (n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* handle error */
			return ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP;
		}
		return ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE; 
	} else if(n == 0) {
		/* connection closed by peer,
		 * need cleanup */
		return ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP;
	}
	ins->offset += n;
	if(ins->offset < ins->length) {
		return ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE;
	}
	/* set to new stage 
	 * ready to send the checksums */
	ins->state = DST_CHKSM_HEADER_SENT;
	return ON_WRITE_SEND_CHECKSUM_HEADER_OK;
}

enum {
	ON_WRITE_SEND_CHECKSUMS_OK = 0,
	ON_WRITE_SEND_CHECKSUMS_NOT_COMPLETE,
	ON_WRITE_SEND_CHECKSUMS_NEED_CLEANUP
};

/* on_write_send_checksums
 * Just send the checksums in the buffer
 * DO NOT CHANGE THE STATE IN THIS ROUTINE */
int on_write_send_checksums(rzync_dst_t *ins)
{
	/* send the checksums in the buffer */
	int n = write(ins->sockfd,ins->buf+ins->offset,ins->length-ins->offset);
	if(n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			return ON_WRITE_SEND_CHECKSUMS_NEED_CLEANUP;
		}
		return ON_WRITE_SEND_CHECKSUMS_NOT_COMPLETE;
	} else if(n == 0) {
		return ON_WRITE_SEND_CHECKSUMS_NEED_CLEANUP;
	}
	ins->offset += n;
	if(ins->offset < ins->length) {
		return ON_WRITE_SEND_CHECKSUMS_NOT_COMPLETE;
	}
	return ON_WRITE_SEND_CHECKSUMS_OK;
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

enum {
	PREPARE_RECEIVE_DELTA_FILE_OK = 0,
	PREPARE_RECEIVE_DELTA_FILE_ERR
};
/* prepare_receive_delta_file : 
 * 1) make a tmp file name
 * 2) open the file for write 
 * 3) set to new stage
 * 4) clear the buffer */
int prepare_receive_delta_file(rzync_dst_t *ins)
{
	/* md5 of the original filename as the tmp file name */
	memset(ins->dst_sync_file.tmp_filename,0,TMP_FILE_NAME_LEN);
	md5s_of_str(ins->filename,RZYNC_MAX_NAME_LENGTH,ins->dst_sync_file.tmp_filename);
	ins->dst_sync_file.fd = open(ins->dst_sync_file.tmp_filename,
			O_CREAT | O_TRUNC | O_WRONLY,0660);
	if(ins->dst_sync_file.fd < 0) {
		perror("open tmp file");
		return PREPARE_RECEIVE_DELTA_FILE_ERR;
	}
	ins->length = ins->offset = 0;
	memset(ins->buf,0,RZYNC_BUF_SIZE);
	return PREPARE_RECEIVE_DELTA_FILE_OK;
}

#define CHECKSUMS_NR_IN_TOTAL_BUF	(RZYNC_BUF_SIZE/RZYNC_CHECKSUM_SIZE)
enum {
	PREPARE_CHECKSUMS_NEED_SET_EV_WRITE = 0,
	PREPARE_CHECKSUMS_NEED_SET_EV_READ,
	PREPARE_CHECKSUMS_ERR
};
/* @prepare_checksums 
 * 1) put checksum information to the buffer 
 * 2) notify the caller with different return value */
int prepare_checksums(rzync_dst_t *ins)
{
	int checksums_left = ins->dst_local_file.block_nr - ins->dst_local_file.checksum_sent;
	if(checksums_left == 0) {
		/* If all checksums have been sent,
		 * 1) set to new stage 
		 * 2) prepare to receive delta-file 
		 * 3) tell the caller to add ev_read */
		if(prepare_receive_delta_file(ins) == PREPARE_RECEIVE_DELTA_FILE_ERR) {
			return PREPARE_CHECKSUMS_ERR;
		}
		ins->state = DST_CHKSM_ALL_SENT;
		return PREPARE_CHECKSUMS_NEED_SET_EV_READ;
	}

	int how_many_to_send_this_time; 
	if(CHECKSUMS_NR_IN_TOTAL_BUF < checksums_left) {
		/* most of the cases */
		how_many_to_send_this_time = CHECKSUMS_NR_IN_TOTAL_BUF;
	}else {
		/* for the last groups of checksums */
		how_many_to_send_this_time = checksums_left;
	}

	memset(ins->buf,0,RZYNC_BUF_SIZE);
	int i;
	char *p = ins->buf;
	/* calculate the checksums and pack them into the buffer */
	for(i=0;i<how_many_to_send_this_time;i++) {
		char fbuf[RZYNC_BLOCK_SIZE];
		memset(fbuf,0,RZYNC_BLOCK_SIZE);
		if(read(ins->dst_local_file.fd,fbuf,RZYNC_BLOCK_SIZE) != RZYNC_BLOCK_SIZE) {
			perror("read file");
			return PREPARE_CHECKSUMS_ERR;
		}
		checksum_t chksm;
		memset(&chksm,0,sizeof(chksm));
		chksm.block_nr = ins->dst_local_file.checksum_sent + i;
		chksm.rcksm = adler32_direct(fbuf,RZYNC_BLOCK_SIZE);
		md5s_of_str(fbuf,RZYNC_BLOCK_SIZE,chksm.md5);
		snprintf(p,RZYNC_CHECKSUM_SIZE,"$%u\n$%u\n$%u\n$%s\n",
				chksm.block_nr,
				chksm.rcksm.rolling_AB.A,
				chksm.rcksm.rolling_AB.B,
				chksm.md5);
		p += RZYNC_CHECKSUM_SIZE;
	}
	ins->offset = 0;
	ins->length = how_many_to_send_this_time * RZYNC_CHECKSUM_SIZE;
	ins->dst_local_file.checksum_sent += how_many_to_send_this_time;
	return PREPARE_CHECKSUMS_NEED_SET_EV_WRITE;
}

/* on_write : Called when client socket is writable */
void on_write(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t *)arg;
	assert(sock == ins->sockfd);
	switch(ins->state) {
		case DST_INIT:
			/* undefined */
			break;
		case DST_REQ_RECEIVED:
			{
				/* For the three possible return value:
				 * -----------------------------------------------
				 * ON_WRITE_SEND_CHECKSUM_HEADER_OK
				 *  header sent ok, if ins->dst_local_file.block_nr > 0
				 *  now prepare to send the checksums
				 *  else set the ev_read
				 * -----------------------------------------------
				 * ON_WRITE_SEND_CHECKSUM_HEADER_NOT_COMPLETE
				 *  header not sent completely, need re-add ev_write
				 *  and don't change state right now
				 * -----------------------------------------------
				 * ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP
				 *  some error happened, need some cleanup..
				 */
				/* checksum header now in the buffer */
				int i = on_write_send_checksum_header(ins);
				if(i == ON_WRITE_SEND_CHECKSUM_HEADER_OK) {
					/* when checksum header sent ok, prepare to send checksums
					 * Basically it does the following :
					 * 1) open the dst_local_file 
					 * 2) lseek to start
					 * 3) set checksum_sent = 0 */
					assert(ins->state == DST_CHKSM_HEADER_SENT);
					if(prepare_send_checksums(ins) == PREPARE_SEND_CHECKSUMS_NOT_OK) {
						goto clean_up;
					}
					/* And then prepare the very first group of checksums */
					int m = prepare_checksums(ins);
					if( m == PREPARE_CHECKSUMS_NEED_SET_EV_WRITE) {
						goto re_add_ev_write;
					}else if(m == PREPARE_CHECKSUMS_NEED_SET_EV_READ) {
						/* no checksums need to be sent */
						goto re_add_ev_read;
					} else {
						goto clean_up;
					}

				}else if(i == ON_WRITE_SEND_CHECKSUM_HEADER_NEED_CLEANUP) {
					/* goto cleanup if needed... */
					goto clean_up;
				} else {
					goto re_add_ev_write;
				}
				/* set ev_write for 
				 * 1) the imcompletely sent checksum header 
				 * 2) checksums which already in the buffer */
			}
			break;
		case DST_CHKSM_HEADER_SENT:
			/* send the checksums now in the buf
			 * prepare other checksums if needed */
			{
				int i = on_write_send_checksums(ins);
				if(i == ON_WRITE_SEND_CHECKSUMS_NOT_COMPLETE) {
					goto re_add_ev_write;
				} else if(i == ON_WRITE_SEND_CHECKSUMS_NEED_CLEANUP) {
					goto clean_up;
				}

				/* checksums in the buffer all sent 
				 * new checksums can be put into buf and sent */
				i = prepare_checksums(ins);
				if(i == PREPARE_CHECKSUMS_NEED_SET_EV_WRITE) {
					/* There're still some checksums to be sent */
					goto re_add_ev_write;
				} else if(i == PREPARE_CHECKSUMS_NEED_SET_EV_READ) {
					/* All checksums have been sent 
					 * Get ready to receive the delta-file */
					goto re_add_ev_read;
				} else {
					goto clean_up;
				}
			}
			break;
		case DST_CHKSM_ALL_SENT:
			/* undefined */
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

enum {
	ON_READ_RECV_DELTA_FILE_NEED_ADD_EV_READ = 0,
	ON_READ_RECV_DELTA_FILE_DONE,
	ON_READ_RECV_DELTA_FILE_NEED_CLEANUP
};
int on_read_receive_delta_file(rzync_dst_t *ins)
{
	// for test
	read(ins->sockfd,ins->buf,RZYNC_BUF_SIZE);
	printf("on_read_receive_delta_file : %s\n",ins->buf);
	return ON_READ_RECV_DELTA_FILE_DONE;
}

/* on_read : Once a client socket is readable,
 * operate differently according to the current state,
 * more specifically :
 * 1) DST_INIT : read the sync-req from src side
 * 2) DST_CHKSM_ALL_SENT : read the delta-file from src
 *
 * Note : currently the other states on-read are not defined */
void on_read(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t*)arg;
	assert(sock == ins->sockfd);
	switch(ins->state) {
		case DST_INIT:
			/* read RZYNC_FILE_INFO */
			{
				int i = on_read_init(ins);
				if(i == ON_READ_INIT_COMPLETE) {
					/* request received successfully 
					 * PREPARE THE CHECKSUM HEADER AND
					 * SET EV_WRITE */
					assert(ins->state == DST_REQ_RECEIVED);
					i = on_read_prepare_checksum_header(ins);
					if(i == ON_READ_PREPARE_CHECKSUM_HEADER_NEED_CLEANUP) {
						goto clean_up;
					}
					return;
				}else if(i == ON_READ_INIT_NOT_COMPLETE) {
					/* go on to complete reading the request */
					goto re_add_ev_read;
				} else if(i == ON_READ_INIT_ERR_NEED_CLEADUP) {
					goto clean_up;
				}
			}
		case DST_REQ_RECEIVED:
			/* undefined */
			break;
		case DST_CHKSM_HEADER_SENT:
			/* undefined */
			break;
		case DST_CHKSM_ALL_SENT:
			/* ready to receive delta file */
			{
				int i = on_read_receive_delta_file(ins);
				if(i == ON_READ_RECV_DELTA_FILE_DONE) {
					goto clean_up;
				}
				// parse_delta_file(ins);
			}
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

/* on_conenct : When new connection comes,
 * 1) Get one rzync_src_t for it
 * 2) Set the state to DST_INIT
 * 3) Set the length and offset to 0 
 * 4) Add read event to the client socket 
 *	  get ready to read sync-req from src */
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

	/* get rzync_dst_t for new connection */
	rzync_dst_t *ins = get_rzyncdst(free_list);
	if(!ins) {
		fprintf(stderr,"get_rzyncdst fail!\n");
		return;
	}
	memset(ins,0,sizeof(rzync_dst_t));
	ins->sockfd = connfd;
	ins->state = DST_INIT;	// set state to DST_INIT 
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

	/* the loop */
	while(g_continue_flag) {
		event_base_loop(ev_base,0);
	}

	/* cleanup stuff */
	event_base_free(ev_base);
	rzyncdst_freelist_destory(free_list);

	return 0;
}

