#include "rzync.h"

#define TEST_BLK_SZ	4096

unsigned char buf[BUFSIZ];

int main(int argc, char* argv[])
{
	if(argc != 2) {
		fprintf(stderr,"Usage : ./rolling <file>\n");
		return 1;
	}

	char* file = argv[1];

	struct stat stt;
	if(stat(file,&stt) != 0) {
		perror("stat");
		return 1;
	}

	unsigned long long size = stt.st_size;

	unsigned long long bytes_processed = 0;
	unsigned int buf_len = 0;
	unsigned int buf_off = 0;

	int fd = open(file,O_RDONLY);
	if(fd < 0) {
		perror("open file");
		return 1;
	}

	int n = read(fd,buf,BUFSIZ);
	if(n < 0) {
		perror("read file");
		return 1;
	}

	buf_len = n;
	if(buf_len < TEST_BLK_SZ) {
	}

	while(bytes_processed != size) {
	}

	rolling_checksum_t directed,rollin;

}

