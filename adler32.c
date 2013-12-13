#include "rzync.h"
#define ADLER_MOD	65521
#define RZYNC_BLOCK_SIZE	16

/* implementation of adler32 algorithms */
/* algorithms specification :
 * A = (1 + D1 + D2 +...+ Dn) mod 65521
 * B = (n*D1 + (n-1)*D2 +...+ Dn + n) mod 65521
 * */

typedef union {
	unsigned int rolling_checksum;
	struct {
		unsigned short A;
		unsigned short B;
	} rolling_AB;
} rolling_checksum_t;

/* direct calculation of adler32 */
unsigned int adler32_direct(unsigned char *buf,int n)
{
	rolling_checksum_t rcksm;
	unsigned int A = 1,B = n;
	int i;
	for(i=0;i<n;i++) {
		unsigned char ch = buf[i];
		A += ch;
		B += (ch * (n - i));
	}
//	A %= ADLER_MOD;
//	B %= ADLER_MOD;
	rcksm.rolling_AB.A = A%ADLER_MOD;
	rcksm.rolling_AB.B = B%ADLER_MOD;
//	printf("A -- %u B -- %u\n",A,B);
//	return (B*(1<<16)+A);
	return (unsigned int)rcksm;
}

unsigned int adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,unsigned int prev_adler)
{
//	unsigned int B = prev_adler >> 16;
//	unsigned int A = prev_adler - B*(1<<16);
//	unsigned int AA = prev_adler & 0x00ff;
	rolling_checksum_t rcksm = (rolling_checksum_t)prev_adler;
	unsigned int B = rcksm.rolling_AB.B;
	unsigned int A = rcksm.rolling_AB.A;
	printf("prev_adler -- %u prevA -- %u prevB -- %u \n",prev_adler,A,B);
//	unsigned int newA = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
//	unsigned int newB = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
//	printf("A -- %u B -- %u\n",newA,newB);
	rcksm.rolling_AB.A = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
	rcksm.rolling_AB.B = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
	return (unsigned int)rcksm;
//	return (newB*(1<<16)+newA);
}

int main()
{
//	char buf[BUFSIZ];
//	int fd = open("md5.c",O_RDONLY);
//	if(fd < 0) {
//		perror("open");
//		return 1;
//	}
//
//	int n = read(fd,buf,BUFSIZ);
//	if(n < 0) {
//		perror("read");
//		return 1;
//	}
//
//	close(fd);
	char buf[32] = "helloworldthishfjkahflkalkfaj";
	printf("%s\n",buf);
	int n = strlen(buf);

	int end_idx = RZYNC_BLOCK_SIZE - 1;
	int start_idx = 0;
	unsigned int adlerv = adler32_direct(buf,RZYNC_BLOCK_SIZE);
	printf("first block -- %u\n",adlerv);
	while(end_idx < n) {
		unsigned char old_ch = buf[start_idx++];
		unsigned char new_ch = buf[++end_idx];
		unsigned int adlv_direct = adler32_direct(buf+start_idx,RZYNC_BLOCK_SIZE);
		unsigned int adlv_rolling = adler32_rolling(old_ch,new_ch,RZYNC_BLOCK_SIZE,adlerv);
		if(adlv_direct != adlv_rolling) {
			fprintf(stderr,"ROLLING DOES NOT EUQAL TO DIRECT CALCULATED VALUE!\n");
			return 1;
		}
		adlerv = adlv_rolling;
	}
	return 0;
}
