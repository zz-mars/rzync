#include "rzync.h"

int main()
{
	printf("%d %s %0x \n",INADDR_ANY,INADDR_ANY,INADDR_ANY);
	struct sockaddr_in addr;
	int addr_len = sizeof(addr);

	memset(&addr,0,addr_len);
	addr.sin_family = AF_INET;
//	inet_pton(AF_INET,ZZIP,(void*)&addr.sin_addr);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(ZZPORT);

	int fd = socket(AF_INET,SOCK_STREAM,0);

	if(fd < 0) {
		perror("socket");
		return 1;
	}
	
	if(connect(fd,(struct sockaddr*)&addr,addr_len) != 0) {
		perror("connect");
		return 1;
	}

	close(fd);
	return 0;
}

