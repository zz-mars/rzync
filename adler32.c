#include "rzync.h"


int main()
{
	char buf[BUFSIZ];
	int fd = open("md5.c",O_RDONLY);
	if(fd < 0) {
		perror("open");
		return 1;
	}

	int n = read(fd,buf,BUFSIZ);
	if(n < 0) {
		perror("read");
		return 1;
	}

	close(fd);
	
//	char buf[32] = "helloworldthishfjkahflkalkfaj";
//	int n = strlen(buf);

//	printf("%s -- %u\n",buf,n);

	int end_idx = RZYNC_BLOCK_SIZE - 1;
	int start_idx = 0;
	rolling_checksum_t adlerv = adler32_direct(buf,RZYNC_BLOCK_SIZE);
	while(end_idx < n) {
		unsigned char old_ch = buf[start_idx++];
		unsigned char new_ch = buf[++end_idx];
		rolling_checksum_t adlv_direct = adler32_direct(buf+start_idx,RZYNC_BLOCK_SIZE);
		rolling_checksum_t adlv_rolling = adler32_rolling(old_ch,new_ch,RZYNC_BLOCK_SIZE,adlerv);
		if(!ROLLING_EQUAL(adlv_direct,adlv_rolling)) {
			fprintf(stderr,"ROLLING DOES NOT EUQAL TO DIRECT CALCULATED VALUE!\n");
			return 1;
		}
		adlerv = adlv_rolling;
	}
	return 0;
}
