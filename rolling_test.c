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
	unsigned int total_in_buf = 0;
	unsigned int in_buf_not_processed = 0;

	while(bytes_processed != size) {
	}

	rolling_checksum_t directed,rollin;

}

