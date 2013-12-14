#include "rzync.h"
/* why 65521 does not work while 2^16 do? */
//#define ADLER_MOD			(1<<16)
#define ADLER_MOD			65521
#define RZYNC_BLOCK_SIZE	4096

/* Implementation of adler32 algorithms */
/* Algorithms specification :
 * A = (1 + D1 + D2 +...+ Dn) mod ADLER_MOD
 * B = (n*D1 + (n-1)*D2 +...+ Dn + n) mod ADLER_MOD
 *
 * A' = (A + Dn+1 -D1 + ADLER_MOD) mod ADLER_MOD
 * B' = (A + B + Dn+1 + ADLER_MOD - 1 - (n + 1) * D1) mod ADLER_MOD
 * */

/* direct calculation of adler32 */
static inline unsigned int adler32_direct(unsigned char *buf,int n)
{
	/* define A&B to be long long to avoid overflow */
	unsigned long long A = 1;
	unsigned long long B = n;
	int i;
	for(i=0;i<n;i++) {
		unsigned char ch = buf[i];
		A += ch;
		B += (ch * (n - i));
	}
	A %= ADLER_MOD;
	B %= ADLER_MOD;
//	printf("A -- %llu B -- %llu\n",A,B);
	unsigned int rcksm = B * (1<<16) + A;
	return rcksm;
}

/* rolling style calculation */
static inline unsigned int adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,unsigned int prev_adler)
{
	/* there is an assumption here: rolling_hash -- B:A
	 * B takes the most significant 16 bits while
	 * A takes the less significant 16 bits
	 * HOWEVER THIS ASSUMPTION IS WRONG! */
	unsigned int B = prev_adler >> 16;
	unsigned int A = prev_adler - B * (1<<16);	/* very inefficient here! */
	printf("prevA -- %u prevB -- %u\n",A,B);
	unsigned int newA = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
	unsigned int newB = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
	printf("newA -- %u newB -- %u\n",newA,newB);
	return (newB * (1<<16) + newA);
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
	unsigned int adlerv = adler32_direct(buf,RZYNC_BLOCK_SIZE);
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

