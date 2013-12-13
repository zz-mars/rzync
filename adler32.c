#include "rzync.h"
#define ADLER_MOD			(1<<16)
#define RZYNC_BLOCK_SIZE	8000

/* Implementation of adler32 algorithms */
/* Algorithms specification :
 * A = (1 + D1 + D2 +...+ Dn) mod ADLER_MOD
 * B = (n*D1 + (n-1)*D2 +...+ Dn + n) mod ADLER_MOD
 *
 * A' = (A + Dn+1 -D1 + ADLER_MOD) mod ADLER_MOD
 * B' = (A + B + Dn+1 + ADLER_MOD - 1 - (n + 1) * D1) mod ADLER_MOD
 * */

typedef union {
	unsigned int rolling_checksum;
	struct {
		unsigned short AB[2];
//		unsigned short B;
	} rolling_AB;
} rolling_checksum_t;

#define ROLLING_EQUAL(x,y)	(x.rolling_checksum == y.rolling_checksum)

/* direct calculation of adler32 */
static inline rolling_checksum_t adler32_direct(unsigned char *buf,int n)
{
	/* define A&B to be long long to avoid overflow */
	unsigned long long A = 1;
	unsigned long long B = n;
//	unsigned int A = 1;
//	unsigned int B = n;
	int i;
	for(i=0;i<n;i++) {
		unsigned char ch = buf[i];
		A += ch;
		B += (ch * (n - i));
	}
	rolling_checksum_t rcksm;
	rcksm.rolling_AB.AB[0] = A%ADLER_MOD;
	rcksm.rolling_AB.AB[1] = B%ADLER_MOD;
	printf("direct -- %u A -- %u B -- %u \n",rcksm.rolling_checksum,rcksm.rolling_AB.AB[0],rcksm.rolling_AB.AB[1]);
	return rcksm;
}

/* rolling style calculation */
static inline rolling_checksum_t adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,rolling_checksum_t prev_adler)
{
	unsigned int A = prev_adler.rolling_AB.AB[0];
	unsigned int B = prev_adler.rolling_AB.AB[1];
	printf("rolling -- %u -- prevA -- %u prevB -- %u \n",prev_adler.rolling_checksum,A,B);
	rolling_checksum_t rcksm;
	rcksm.rolling_AB.AB[0] = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
	rcksm.rolling_AB.AB[1] = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
	printf("rolling -- %u A -- %u B -- %u \n",rcksm.rolling_checksum,rcksm.rolling_AB.AB[0],rcksm.rolling_AB.AB[1]);
	return rcksm;
}

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

	/*
	char buf[32] = "helloworldthishfjkahflkalkfaj";
	int n = strlen(buf);
	*/

	printf("%s -- %u\n",buf,n);

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
