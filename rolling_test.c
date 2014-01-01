#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "adler32.h"

#define TEST_BLK_SZ	4096

#define BUFSZ	(1<<16)

#define ROLLING_EQUAL(x,y)	(x.rolling_checksum == y.rolling_checksum)

unsigned char buf[BUFSZ];

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
	if(size < TEST_BLK_SZ) {
		fprintf(stderr,"file too small..\n");
		return 1;
	}

	unsigned long long bytes_processed = 0;

	int fd = open(file,O_RDONLY);
	if(fd < 0) {
		perror("open file");
		return 1;
	}

	int n = read(fd,buf,BUFSZ);
	if(n < 0) {
		perror("read file");
		return 1;
	}
	close(fd);

	unsigned int buf_len = n;
	printf("%d bytes read..\n",buf_len);

	unsigned int a = adler32_checksum(buf,TEST_BLK_SZ);
	printf("first blk -- %u\n",a);


	unsigned int b = adler32_checksum(buf+1,TEST_BLK_SZ);
	printf("second blk -- %u\n",b);
	unsigned char c1 = buf[0];
	unsigned char c2 = buf[TEST_BLK_SZ];

	unsigned int c = adler32_rolling_checksum(a,TEST_BLK_SZ,c1,c2);
	printf("second blk rolling -- %u\n",c);

	//	return 0;

	unsigned int blk_s = 0;
	unsigned int blk_e = blk_s + TEST_BLK_SZ;
	/* calculate directly for the first block */
	unsigned int x = 
		adler32_checksum(buf+blk_s,TEST_BLK_SZ);
	/* rolling! */
	while(blk_e < buf_len) {
		c2 = buf[blk_e++];
		c1 = buf[blk_s++];
		unsigned int y =
			adler32_rolling_checksum(x,TEST_BLK_SZ,c1,c2);
		unsigned int dire = 
			adler32_checksum(buf+blk_s,TEST_BLK_SZ);
		if(y != dire) {
			fprintf(stderr,"rolling not equal............\n");
			return 1;
		}
		x = y;
	}
	return 0;
}

