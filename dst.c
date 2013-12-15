#include "rzync.h"

void do_new_connection(int sock,short event,void *arg);

int main()
{

	struct sockaddr_in addr;
	int addr_len = sizeof(addr);
	memset(&addr,0,addr_len);
	addr.sin_family = AF_INET;

//	inet_pton(AF_INET,INADDR_ANY,(void*)&addr.sin_addr);
//	inet_pton(AF_INET,ZZIP,(void*)&addr.sin_addr);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(ZZPORT);

	int lfd = socket(AF_INET,SOCK_STREAM,0);

	if(lfd < 0) {
		perror("socket");
		return 1;
	}

	if(bind(lfd,(struct sockaddr*)&addr,addr_len) != 0) {
		perror("bind");
		return 1;
	}

	fcntl(lfd,F_SETFL,O_NONBLOCK);

	listen(lfd,8);

	struct event_base *ev_base = event_base_new();
	if(!ev_base) {
		fprintf(stderr,"event_base_new fail!\n");
		return 1;
	}

	struct event e_lfd;

	event_set(&e_lfd,lfd,EV_READ | EV_PERSIST,do_new_connection,NULL);

	if(event_base_set(ev_base,&e_lfd) != 0) {
		fprintf(stderr,"event_base_set fail!\n");
		return 1;
	}

	if(event_add(&e_lfd,NULL) != 0) {
		fprintf(stderr,"event_add fail!\n");
		return 1;
	}

	event_base_loop(ev_base,0);

	/*
	while(1) {
		struct sockaddr_in claddr;
		int claddrlen;

		int connfd = accept(lfd,(struct sockaddr*)&claddr,&claddrlen);

		if(connfd < 0) {
			perror("accept");
			continue;
		}

		printf("new connection comes\n");
		close(connfd);
	}
	*/

	return 0;
}

void do_new_connection(int sock,short event,void *arg)
{
	struct sockaddr_in claddr;
	int claddrlen;

	int connfd = accept(sock,(struct sockaddr*)&claddr,&claddrlen);

	if(connfd < 0) {
		perror("accept");
		return;
	}

	printf("new connection comes\n");
	close(connfd);
	return;
}

