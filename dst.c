#include "rzync.h"
#include "util.h"

static int g_continue_flag = 1; // continue flag, default set to 1
struct event_base *ev_base = NULL;	// initialize to NULL,will be set on start
rzyncdst_freelist_t * free_list = NULL;

void rzyncdst_ins_cleanup(rzync_dst_t *ins)
{
	close(ins->filefd);
	close(ins->dstfd);
	close(ins->sockfd);
	memset(ins,0,sizeof(rzync_dst_t));
	put_rzyncdst(free_list,ins);
}

void on_read(int sock,short event,void *arg)
{
	rzync_dst_t *ins = (rzync_dst_t*)arg;
	if(read(sock,ins->buf,128) < 0) {
		if(!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			/* handle error */
			goto clean_up;
		}
	}

	printf("from client : %s\n",ins->buf);
	event_set(&ins->sock_read,sock,EV_READ,on_read,(void*)ins);
	if(event_base_set(ev_base,&ins->sock_read) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		goto clean_up;
	}
	if(event_add(&ins->sock_read,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		goto clean_up;
	}
	return;
clean_up:
	rzyncdst_ins_cleanup(ins);
}

void on_conenct(int sock,short event,void *arg)
{
	rzyncdst_freelist_t *fl = (rzyncdst_freelist_t*)arg;
	struct sockaddr_in claddr;
	int claddrlen;

	int connfd = accept(sock,(struct sockaddr*)&claddr,&claddrlen);

	if(connfd < 0) {
		perror("accept");
		return;
	}

	printf("new connection comes\n");
//	close(connfd);
	rzync_dst_t *ins = get_rzyncdst(fl);
	if(!ins) {
		fprintf(stderr,"get_rzyncdst fail!\n");
		return;
	}

	memset(ins,0,sizeof(rzync_dst_t));
	ins->sockfd = connfd;
	ins->state = DST_INIT;
	ins->length = ins->offset = 0;
	
	/* set read event */
	event_set(&ins->sock_read,connfd,EV_READ,on_read,(void*)ins);
	return;
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
	event_set(&e_lfd,lfd,EV_READ | EV_PERSIST,on_conenct,(void*)free_list);
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

	event_base_free(ev_base);
	rzyncdst_freelist_destory(free_list);
	return 0;
}

