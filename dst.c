#include "rzync.h"
#include "util.h"

static int g_continue_flag = 1; // continue flag, default set to 1
struct event_base *ev_base = NULL;	// initialize to NULL,will be set on start
rzyncdst_freelist_t * free_list = NULL;

void rzyncdst_ins_cleanup(rzync_dst_t *ins)
{
	printf("cleanup called................\n");
	close(ins->filefd);
	close(ins->dstfd);
	close(ins->sockfd);
	memset(ins,0,sizeof(rzync_dst_t));
	put_rzyncdst(free_list,ins);
}

enum on_read_rt {
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
		} else { return ON_READ_INIT_ERR_OK; }
	} else if(n == 0) {return ON_READ_INIT_ERR_NEED_CLEADUP;}

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
	ins->state = DST_REQ_RECEIVED;

	return ON_READ_INIT_OK;
}

void on_read(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t*)arg;
	assert(sock == ins->sockfd);
	switch(ins->state) {
		case DST_INIT:
			/* read RZYNC_FILE_INFO */
			{
				enum on_read_rt i = on_read_init(ins);
				if(i == ON_READ_INIT_ERR_NEED_CLEADUP) {
					goto clean_up;
				}
				goto re_add_read_event;
			}
		case DST_REQ_RECEIVED:
			break;
		case DST_CHKSM_HEADER_SENT:
			break;
		case DST_CHKSM_ALL_SENT:
			/* ready to receive delta file */
			break;
		case DST_DELTA_FILE_RECEIVED:
		case DST_DONE:
		default:
			break;
	}

re_add_read_event:
	if(event_add(&ins->ev_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		goto clean_up;
	}
	return;
clean_up:
	rzyncdst_ins_cleanup(ins);
}

void on_write(int sock,short event,void *arg)
{
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

