#include "rzync.h"
#include "util.h"
#include "adler32.h"

/* set default block size to 4KB */
//static unsigned int dst_block_sz = RZYNC_BLOCK_SIZE;

// the original directory
#define RZYNC_ORIGIN_DIR	"rz_origin"		
// the newly synced file will be put into this directory
#define RZYNC_UPDATED_DIR	"rz_updated"

/* set default value */
static unsigned char* origin_dir = RZYNC_ORIGIN_DIR;
static unsigned char* updated_dir = RZYNC_UPDATED_DIR;

static int g_continue_flag = 1; // continue flag, default set to 1
static struct event_base *ev_base = NULL;
static rzyncdst_freelist_t * free_list = NULL;

/* ----------------- rzync_dst_t pool ------------------ */
/* initialize to a null list */
rzyncdst_freelist_t *rzyncdst_freelist_init(void)
{
	rzyncdst_freelist_t *fl = (rzyncdst_freelist_t*)malloc(sizeof(rzyncdst_freelist_t));
	if(!fl) {
		return NULL;
	}
	fl->client_nr = 0;
	fl->pool_head = NULL;
	list_head_init(&fl->free_list);
	return fl;
}

void rzyncdst_freelist_destory(rzyncdst_freelist_t *fl)
{
	rzyncdst_pool_t *p = fl->pool_head;
	while(p) {
		rzyncdst_pool_t *q = p->next;
		if(p->clients) {
			free(p->clients);
		}
		free(p);
		p = q;
	}
	free(fl);
	printf("free list destroyed...........\n");
}

rzync_dst_t *get_rzyncdst(rzyncdst_freelist_t *fl)
{
	rzync_dst_t *cl;
	if(fl->client_nr == 0) {
		/* add more elements */
		rzyncdst_pool_t *cp = (rzyncdst_pool_t*)malloc(sizeof(rzyncdst_pool_t));
		if(!cp) {
			return NULL;
		}
		cp->client_nr = RZYNC_CLIENT_POOL_SIZE;
		cp->next = NULL;
		cp->clients = (rzync_dst_t*)malloc(cp->client_nr*sizeof(rzync_dst_t));
		if(!cp->clients) {
			free(cp);
			return NULL;
		}
		/* insert to free list */
		int i;
		for(i=0;i<cp->client_nr-1;i++) {
			list_add(&cp->clients[i].flist,&fl->free_list);
		}
		/* insert to pool list */
		if(!fl->pool_head) {
			fl->pool_head = cp;
		}else {
			cp->next = fl->pool_head->next;
			fl->pool_head = cp;
		}
		fl->client_nr += (cp->client_nr - 1);
		/* return the last one */
		cl = &cp->clients[cp->client_nr-1];
	} else {
		/* get one directly from free list */
		struct list_head *l = fl->free_list.next;
		list_del(l);
		fl->client_nr--;
		cl = ptr_clientof(l);
	}
	return cl;
}

void put_rzyncdst(rzyncdst_freelist_t *fl,rzync_dst_t *cl)
{
	list_add(&cl->flist,&fl->free_list);
	fl->client_nr++;
}

void rzyncdst_ins_cleanup(rzync_dst_t *ins)
{
	printf("cleanup called................\n");
	close(ins->sockfd);
	/* close file descriptor according its state */
	if(ins->dst_local_file.fd > 0) {
		close(ins->dst_local_file.fd);
	}
	if(ins->dst_sync_file.fd > 0) {
		close(ins->dst_sync_file.fd);
	}
	memset(ins,0,sizeof(rzync_dst_t));
	put_rzyncdst(free_list,ins);
}

enum {
	ON_READ_RECV_SYNC_REQ_COMPLETE = 0,
	ON_READ_RECV_SYNC_REQ_NOT_COMPLETE,
	ON_READ_RECV_SYNC_REQ_ERR
};
/* on_read_recv_sync_req : 
 * receive and parse the req header */
int on_read_recv_sync_req(rzync_dst_t *ins)
{
	int n = read(ins->sockfd,
			ins->buf+ins->length,
			RZYNC_FILE_INFO_SIZE-ins->length);
	if (n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* handle error */
			return ON_READ_RECV_SYNC_REQ_ERR;
		} else { 
			/* not error for non-blocking IO */
			return ON_READ_RECV_SYNC_REQ_NOT_COMPLETE; 
		}
	} else if(n == 0) {
		/* connection closed by peer */
		return ON_READ_RECV_SYNC_REQ_ERR;
	}

	/* when n > 0, update ins->length */
	ins->length += n;
	ins->statistics.total_recved += n;
	if(ins->length < RZYNC_FILE_INFO_SIZE) {
		/* request header not all received 
		 * DO NOT CHANGE THE STATE NOW */
		return ON_READ_RECV_SYNC_REQ_NOT_COMPLETE;
	}

	/* parse the request */
	char *p = ins->buf;
//	printf("rsync -- \n%s\n",p);
	/* file name length */
	int filenamelen = str2i(&p,'#','\n');
	if(filenamelen == STR2I_PARSE_FAIL ||
			filenamelen > (RZYNC_MAX_NAME_LENGTH-1) ||
			*p++ != '$') {
		/* illegal request */
		return ON_READ_RECV_SYNC_REQ_ERR;
	}
	/* file name */
	memset(ins->filename,0,RZYNC_MAX_NAME_LENGTH);
	strncpy(ins->filename,p,filenamelen);
	p += (filenamelen+1);
	/* file size */
	long long fsize = str2ll(&p,'$','\n');
	if(fsize == STR2LL_PARSE_FAIL) {
		return ON_READ_RECV_SYNC_REQ_ERR;
	}
	ins->size = fsize;
	int src_blk_sz = str2i(&p,'$','\n');
	if(src_blk_sz == STR2I_PARSE_FAIL) {
		return ON_READ_RECV_SYNC_REQ_ERR;
	}
	ins->dst_local_file.block_sz = src_blk_sz;

	long long mtime = str2ll(&p,'$','\n');
	if(mtime == STR2LL_PARSE_FAIL) {
		return ON_READ_RECV_SYNC_REQ_ERR;
	}
	ins->mtime = mtime;
	/* md5 */
	if(*p++ != '$') {
		return ON_READ_RECV_SYNC_REQ_ERR;
	}
	strncpy(ins->md5,p,RZYNC_MD5_CHECK_SUM_BITS);
	ins->md5[RZYNC_MD5_CHECK_SUM_BITS] = '\0';
	/* request parse ok */
	return ON_READ_RECV_SYNC_REQ_COMPLETE;
}

enum {
	SEND_DST_BUF_OK = 0,
	SEND_DST_BUF_NOT_COMPLETE,
	SEND_DST_BUF_ERR
};
/* send_dst_buf : send the data in dst buf via Socket */
int send_dst_buf(rzync_dst_t *ins)
{
	int n = write(ins->sockfd,ins->buf+ins->offset,ins->length-ins->offset);
	if (n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* unacceptable error */
			return SEND_DST_BUF_ERR;
		}
		return SEND_DST_BUF_NOT_COMPLETE; 
	} else if(n == 0) {
		/* connection closed by peer */
		return SEND_DST_BUF_ERR;
	}
	ins->statistics.total_sent += n;
	ins->offset += n;
	if(ins->offset == ins->length) {
		return SEND_DST_BUF_OK;
	}
//	assert(ins->offset < ins->length);
	return SEND_DST_BUF_NOT_COMPLETE;
}

/* @prepare_send_checksums 
 * 1) lseek to start
 * 2) set checksum_sent to 0 */
inline void prepare_send_checksums(rzync_dst_t *ins)
{
	/* open dst_local_file and set checksum_sent to 0 */
//	ins->dst_local_file.fd = open(ins->filename,O_RDONLY);
//	if(ins->dst_local_file.fd < 0) {
//		perror("open dst_local_file");
//		return PREPARE_SEND_CHECKSUMS_NOT_OK;
//	}
	lseek(ins->dst_local_file.fd,0,SEEK_SET);
	ins->dst_local_file.checksum_sent = 0;	// initialize to 0
}

enum {
	PREPARE_RECEIVE_DELTA_FILE_OK = 0,
	PREPARE_RECEIVE_DELTA_FILE_ERR
};
/* prepare_receive_delta_file : 
 * 1) make a tmp file name
 * 2) open the file for write 
 * 4) clear the buffer */
int prepare_receive_delta_file(rzync_dst_t *ins)
{
	if(access(updated_dir,F_OK) != 0) {
		if(errno != ENOENT) {
			perror("updated dir");
			return PREPARE_RECEIVE_DELTA_FILE_ERR;
		}
		if(mkdir(updated_dir,0770) != 0) {
			perror("mkdir");
			return PREPARE_RECEIVE_DELTA_FILE_ERR;
		}
	}

	memset(ins->dst_sync_file.tmp_filename,0,TMP_FILE_NAME_LEN);
	snprintf(ins->dst_sync_file.tmp_filename,
			TMP_FILE_NAME_LEN,
			"%s/%s",updated_dir,ins->filename);
	ins->dst_sync_file.fd = open(ins->dst_sync_file.tmp_filename,
			O_CREAT | O_TRUNC | O_WRONLY,0660);
	if(ins->dst_sync_file.fd < 0) {
		perror("open tmp file");
		return PREPARE_RECEIVE_DELTA_FILE_ERR;
	}
	ins->length = ins->offset = 0;
	ins->dst_sync_file.bytes_recvd = 0;
	memset(ins->buf,0,RZYNC_BUF_SIZE);
	return PREPARE_RECEIVE_DELTA_FILE_OK;
}

#define CHECKSUMS_NR_IN_TOTAL_BUF	(RZYNC_BUF_SIZE/RZYNC_CHECKSUM_SIZE)
enum {
	PREPARE_CHECKSUMS_OK = 0,
	PREPARE_CHECKSUMS_NO_MORE_TO_SEND,
	PREPARE_CHECKSUMS_ERR
};
/* @prepare_checksums 
 * 1) put checksum information to the buffer 
 * 2) notify the caller with different return value */
int prepare_checksums(rzync_dst_t *ins)
{
	unsigned int dst_block_sz = ins->dst_local_file.block_sz;
	int checksums_left = ins->dst_local_file.block_nr - ins->dst_local_file.checksum_sent;
//	assert(checksums_left >= 0);
	if(checksums_left == 0) {
		return PREPARE_CHECKSUMS_NO_MORE_TO_SEND;
	}

	char* fbuf = malloc(dst_block_sz);
	if(!fbuf) {
		return PREPARE_CHECKSUMS_ERR;
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
		/* clear file buf */
		memset(fbuf,0,dst_block_sz);
	//	/* lseek to the right position */
	//	lseek(ins->dst_local_file.fd,chksm.block_nr*dst_block_sz,SEEK_SET);
		if(read(ins->dst_local_file.fd,fbuf,dst_block_sz) != dst_block_sz) {
			perror("read file");
			free(fbuf);
			return PREPARE_CHECKSUMS_ERR;
		}
		/* set block nr */
		unsigned int chksm_block_nr = ins->dst_local_file.checksum_sent + i;
		/* calculate rolling chcksm */
		unsigned int chksm_rcksm = adler32_checksum(fbuf,dst_block_sz);
		/* calculate md5 */
		unsigned char chksm_md5[RZYNC_MD5_CHECK_SUM_BITS+1];
		memset(chksm_md5,0,RZYNC_MD5_CHECK_SUM_BITS+1);
		md5s_of_str(fbuf,dst_block_sz,chksm_md5);
		/* put into buffer */
		snprintf(p,RZYNC_CHECKSUM_SIZE,"$%u\n$%u\n$%s\n",
				chksm_block_nr,chksm_rcksm,chksm_md5);
	//	printf("%s",p);
		p += RZYNC_CHECKSUM_SIZE;
	}
	ins->offset = 0;
	ins->length = how_many_to_send_this_time * RZYNC_CHECKSUM_SIZE;
	ins->dst_local_file.checksum_sent += how_many_to_send_this_time;
	free(fbuf);
	return PREPARE_CHECKSUMS_OK;
}

/* on_write : Called when client socket is writable */
void on_write(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t *)arg;
	switch(ins->state) {
		case DST_INIT:
			/* undefined */
			break;
		case DST_REQ_RECEIVED:
			{
				/* checksum header now in the buffer */
				int i = send_dst_buf(ins);
				if(i == SEND_DST_BUF_OK) {
					/* when checksum header sent ok, prepare to send checksums
					 * Basically it does the following :
					 * 1) open the dst_local_file 
					 * 2) lseek to start
					 * 3) set checksum_sent = 0 */
					ins->state = DST_CHKSM_HEADER_SENT;
					prepare_send_checksums(ins);
					/* And then prepare the very first group of checksums */
					int m = prepare_checksums(ins);
					if( m == PREPARE_CHECKSUMS_OK) {
						goto re_add_ev_write;
					}else if(m == PREPARE_CHECKSUMS_NO_MORE_TO_SEND) {
						/* All checksums are sent, prepare to receive delta */
						if(prepare_receive_delta_file(ins) == PREPARE_RECEIVE_DELTA_FILE_ERR) {
							goto clean_up;
						}
						ins->state = DST_CHKSM_ALL_SENT;
						goto re_add_ev_read;
					} else {
						goto clean_up;
					}
				}else if(i == SEND_DST_BUF_ERR) {
					goto clean_up;
				} else {
					/* checksum header sent not complete */
					goto re_add_ev_write;
				}
			}
			break;
		case DST_CHKSM_HEADER_SENT:
			/* send the checksums now in the buf
			 * prepare other checksums if needed */
			{
				int i = send_dst_buf(ins);
				if(i == SEND_DST_BUF_NOT_COMPLETE) {
					goto re_add_ev_write;
				} else if(i == SEND_DST_BUF_ERR) {
					goto clean_up;
				}
				/* checksums in buffer all sent, prepare for more */
				i = prepare_checksums(ins);
				if(i == PREPARE_CHECKSUMS_OK) {
					goto re_add_ev_write;
				} else if(i == PREPARE_CHECKSUMS_NO_MORE_TO_SEND) {
					if(prepare_receive_delta_file(ins) == PREPARE_RECEIVE_DELTA_FILE_ERR) {
						goto clean_up;
					}
					ins->state = DST_CHKSM_ALL_SENT;
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
			break;
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
//	printf("cleanup from on_write...........\n");
	rzyncdst_ins_cleanup(ins);
}

enum {
	ON_READ_PREPARE_CHECKUM_HEADER_OK = 0,
	ON_READ_PREPARE_CHECKUM_HEADER_ERR
};
/* Prepare the checksum header in the buffer */
int on_read_prepare_checksum_header(rzync_dst_t *ins)
{
	/* the original file */
	unsigned char tmp_filename[TMP_FILE_NAME_LEN];
	memset(tmp_filename,0,TMP_FILE_NAME_LEN);
	snprintf(tmp_filename,TMP_FILE_NAME_LEN,
			"%s/%s",origin_dir,ins->filename);

	ins->dst_local_file.fd = open(tmp_filename,O_RDONLY);
	if(ins->dst_local_file.fd < 0) {
		perror("open original file");
		return ON_READ_PREPARE_CHECKUM_HEADER_ERR;
	}

//	ins->dst_local_file.block_sz = dst_block_sz;
	struct stat stt;
	unsigned long long file_sz = 
		(fstat(ins->dst_local_file.fd,&stt) == 0)?stt.st_size:0;
	ins->dst_local_file.block_nr = file_sz / ins->dst_local_file.block_sz;
	memset(ins->buf,0,RZYNC_BUF_SIZE);
	/* only send the block nr */
	snprintf(ins->buf,
			RZYNC_CHECKSUM_HEADER_SIZE,
			"$%u\n",ins->dst_local_file.block_nr);
	ins->length = RZYNC_CHECKSUM_HEADER_SIZE;
	ins->offset = 0;
	return ON_READ_PREPARE_CHECKUM_HEADER_OK;
}

enum {
	ON_READ_RECV_DELTA_FILE_OK = 0,
	ON_READ_RECV_DELTA_FILE_NEED_CLEANUP
};
/* on_read_receive_delta_file : 
 * Read as much data as possible each time */
int on_read_receive_delta_file(rzync_dst_t *ins)
{
	int n = read(ins->sockfd,
			ins->buf+ins->length,
			RZYNC_BUF_SIZE-ins->length);
	if(n < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			return ON_READ_RECV_DELTA_FILE_NEED_CLEANUP;
		}
		return ON_READ_RECV_DELTA_FILE_OK;
	}else if(n == 0) {
		/* connection closed by peer */
		return ON_READ_RECV_DELTA_FILE_NEED_CLEANUP;
	}
	ins->statistics.total_recved += n;
	ins->length += n;
	return ON_READ_RECV_DELTA_FILE_OK;
}

typedef struct {
	char flag;
	unsigned int nr;
	unsigned int header_length;
} delta_header_t;
enum {
	PARSE_DELTA_HEADER_OK = 0,
	PARSE_DELTA_HEADER_INCOMPLETE_HEADER,
	PARSE_DELTA_HEADER_ERR
};
/* Try to parse a valid delta header from the buffer rigion */
int parse_delta_header(char *buf,unsigned int sidx,unsigned int eidx,delta_header_t *dh)
{
	int buf_len = eidx - sidx;
//	assert(buf_len >= 0);

	int i;
	for(i=sidx;i<eidx;i++) {
		if(buf[i] == '\n') {
			/* end of delta header found */
			break;
		}
	}
	if(i == eidx) {
		return PARSE_DELTA_HEADER_INCOMPLETE_HEADER;
	}
	/* Tail of delta header encountered, 
	 * Maybe there exists a valid header,
	 * Let's parse it! */

	char *p = buf + sidx;
	if(*p++ != '$') {
		return PARSE_DELTA_HEADER_ERR;
	}
	dh->flag = *p++;
	if(!(dh->flag == DELTA_DUP || dh->flag == DELTA_NDUP)) {
		return PARSE_DELTA_HEADER_ERR;
	}
	dh->nr = str2i(&p,'$','\n');
	if(dh->nr == STR2I_PARSE_FAIL) {
		return PARSE_DELTA_HEADER_ERR;
	}
	dh->header_length = (p - buf) - sidx;
	return PARSE_DELTA_HEADER_OK;
}

enum {
	PARSE_DELTA_FILE_READ_MORE = 0,
	PARSE_DELTA_FILE_ALL_DONE,
	PARSE_DELTA_FILE_ERR
};
/* parse_delta_file */
int parse_delta_file(rzync_dst_t *ins)
{
	unsigned int dst_block_sz = ins->dst_local_file.block_sz;
	int local_fd = ins->dst_local_file.fd;
	int sync_fd = ins->dst_sync_file.fd;
	char* dup_buf = malloc(dst_block_sz);
	if(!dup_buf) {
		perror("malloc for block buf");
		return PARSE_DELTA_FILE_ERR;
	}
	int ret = PARSE_DELTA_FILE_ERR;
	while(1) {
		delta_header_t dh;
		int i = parse_delta_header(ins->buf,ins->offset,ins->length,&dh);
		if(i == PARSE_DELTA_HEADER_ERR) {
			goto free_dup_buf_and_ret;
		}else if(i == PARSE_DELTA_HEADER_INCOMPLETE_HEADER) {
			goto need_read_more;
		}
		char *wbuf;
		unsigned int wbytes;
		unsigned int offset_adjust;
		/* parse header ok */
		if(dh.flag == DELTA_DUP) {
			/* dup block found */
			memset(dup_buf,0,dst_block_sz);
			/* find the right place to read */
			lseek(local_fd,dst_block_sz*dh.nr,SEEK_SET);
			/* read one block */
			int n = read(local_fd,dup_buf,dst_block_sz);
			if(n != dst_block_sz) {
				perror("read one block from local file");
				goto free_dup_buf_and_ret;
			}
			wbuf = dup_buf;
			wbytes = dst_block_sz;
			offset_adjust = dh.header_length;
		}else {
			/* non-dup block */
			unsigned int data_sidx = ins->offset + dh.header_length;	// the idx of non-dup data starts
			int available_bytes = ins->length - data_sidx;
			if(available_bytes < dh.nr) {
				goto need_read_more;
			} 
			wbuf = ins->buf + data_sidx;
			wbytes = dh.nr;
			offset_adjust = dh.header_length + dh.nr;
		}
		/* write data to the dest file */
		if(write(sync_fd,wbuf,wbytes) != wbytes) {
			perror("write to sync file");
			goto free_dup_buf_and_ret;
		}
		/* update the bytes_recvd */
		ins->dst_sync_file.bytes_recvd += wbytes;
	//	assert(ins->dst_sync_file.bytes_recvd <= ins->size);
		if(ins->dst_sync_file.bytes_recvd == ins->size) {
			ret = PARSE_DELTA_FILE_ALL_DONE;
			goto free_dup_buf_and_ret;
		}
		/* update the buffer offset */
		ins->offset += offset_adjust;
	//	assert(ins->offset <= ins->length);
		/* continue */
	}
free_dup_buf_and_ret:
	free(dup_buf);
	return ret;
need_read_more:
	/* move the data in the buffer to the very beginning of buffer
	 * and return read more */
	free(dup_buf);
	{
		int now_in_buf = ins->length - ins->offset;
	//	assert(now_in_buf >= 0);
		if(now_in_buf > 0) {
			char *tmp_buf = (char*)malloc(now_in_buf);
			if(!tmp_buf) {
				return PARSE_DELTA_FILE_ERR;
			}
			memset(tmp_buf,0,now_in_buf);
			memcpy(tmp_buf,ins->buf+ins->offset,now_in_buf);
			memset(ins->buf,0,RZYNC_BUF_SIZE);
			memcpy(ins->buf,tmp_buf,now_in_buf);
			free(tmp_buf);
		}
		ins->length = now_in_buf;
		ins->offset = 0;
	}
	return PARSE_DELTA_FILE_READ_MORE;
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
	switch(ins->state) {
		case DST_INIT:
			/* read RZYNC_FILE_INFO */
			{
				int i = on_read_recv_sync_req(ins);
				if(i == ON_READ_RECV_SYNC_REQ_COMPLETE) {
					/* request received successfully 
					 * PREPARE THE CHECKSUM HEADER AND
					 * SET EV_WRITE */
				//	printf("/* request parse ok, update to new state */\n");
				//	printf("filename -- %s\n",ins->filename);
				//	printf("size -- %llu\n",ins->size);
				//	printf("mtime -- %llu\n",ins->mtime);
				//	printf("md5 -- %s\n",ins->md5);
					/* update the state */
					ins->state = DST_REQ_RECEIVED;
					/* prepare the checksum header */
					i = on_read_prepare_checksum_header(ins);
					if(i == ON_READ_PREPARE_CHECKUM_HEADER_ERR) {
						goto clean_up;
					}
					goto re_add_ev_write;
				}else if(i == ON_READ_RECV_SYNC_REQ_NOT_COMPLETE) {
					/* go on to complete reading the request */
					goto re_add_ev_read;
				} else if(i == ON_READ_RECV_SYNC_REQ_ERR) {
					goto clean_up;
				}
			}
			break;
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
				if(i == ON_READ_RECV_DELTA_FILE_NEED_CLEANUP) {
					goto clean_up;
				}
				i = parse_delta_file(ins);
				if(i == PARSE_DELTA_FILE_READ_MORE) {
					goto re_add_ev_read;
				} else if(i == PARSE_DELTA_FILE_ALL_DONE){
					/* All done, print the statistics */
				//	printf("------------------- dst statistics -------------------\n"); 
				//	printf("total_sent -- %llu\n",ins->statistics.total_sent);
				//	printf("total_recved -- %llu\n",ins->statistics.total_recved);
					/* calculate md5 of file for the final checking */
					char sync_md5[RZYNC_MD5_CHECK_SUM_BITS+1];
					memset(sync_md5,0,RZYNC_MD5_CHECK_SUM_BITS+1);
					if(md5s_of_file(ins->dst_sync_file.tmp_filename,sync_md5) != 0) {
						fprintf(stderr,"Final calculating md5 fail..\n");
						goto clean_up;
					}
					if(strncmp(ins->md5,sync_md5,RZYNC_MD5_CHECK_SUM_BITS) == 0) {
						printf("Synchronization succedded -- %s\n",ins->filename);
					} else {
						fprintf(stderr,"Final md5 checking fail..\n");
					}
				}
				/* goto clean_up on error or ALL_DONE */
				goto clean_up;
			}
			break;
		case DST_DELTA_FILE_RECEIVED:
			break;
		case DST_DONE:
			break;
		default:
			break;
	}
	return;
re_add_ev_read:
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
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
//	printf("cleanup from on_read.................\n");
	rzyncdst_ins_cleanup(ins);
}

/* on_conenct : When new connection comes,
 * 1) Get one rzync_src_t for it
 * 2) Set the state to DST_INIT
 * 3) Set the length and offset to 0 
 * 4) Set and add read event to the client socket 
 *	  get ready to read sync-req from src 
 * 5) Set the write event, do not add */
void on_conenct(int sock,short event,void *arg)
{
	struct sockaddr_in claddr;
	int claddrlen;

	int connfd = accept(sock,(struct sockaddr*)&claddr,&claddrlen);

	if(connfd < 0) {
		if(!(errno == EAGAIN || errno == EWOULDBLOCK)) {
			perror("accept");
		}
		return;
	}

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
	/* file descriptor initialized to -1 */
	ins->dst_local_file.fd = ins->dst_sync_file.fd = -1;
	/* statistics */
	ins->statistics.total_sent = 0;
	ins->statistics.total_recved = 0;
	
	/* set read event */
	event_set(&ins->ev_read,ins->sockfd,EV_READ,on_read,(void*)ins);
	if(event_base_set(ev_base,&ins->ev_read) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		goto clean_up;
	}
	/* add read event */
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		goto clean_up;
	}
	/* set write event,but don't add now */
	event_set(&ins->ev_write,ins->sockfd,EV_WRITE,on_write,(void*)ins);
	if(event_base_set(ev_base,&ins->ev_write) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		goto clean_up;
	}
	return;
clean_up:
//	printf("cleanup from on_conenct..................\n");
	rzyncdst_ins_cleanup(ins);
}


int main(int argc,char *argv[])
{
	if(argc != 3) {
		fprintf(stderr,"Usage : ./rzdst <origin_dir> <updated_dir>\n");
		return 1;
	}

	/* set the origin and updated dir */
	origin_dir = argv[1];
	updated_dir = argv[2];

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

	/* set reuse addr */
	int flag = 1;
	setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));

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

