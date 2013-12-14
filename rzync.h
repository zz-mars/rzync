#ifndef __RZYNC_H
#define __RZYNC_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "list_head.h"

#define RZYNC_BLOCK_SIZE			4096
#define RZYNC_ROLLING_HASH_BITS		32
#define RZYNC_MD5_CHECK_SUM_BITS	32

/* weird bug for ADLER_MOD = 65521 */
#define ADLER_MOD			(1<<16)
//#define ADLER_MOD			65521

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
		unsigned short A;
		unsigned short B;
	} rolling_AB;
} rolling_checksum_t;

#define ROLLING_EQUAL(x,y)	(x.rolling_checksum == y.rolling_checksum)

/* direct calculation of adler32 */
static inline rolling_checksum_t adler32_direct(unsigned char *buf,int n)
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
	rolling_checksum_t rcksm;
	rcksm.rolling_AB.A = A%ADLER_MOD;
	rcksm.rolling_AB.B = B%ADLER_MOD;
	return rcksm;
}

/* rolling style calculation */
static inline rolling_checksum_t adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,rolling_checksum_t prev_adler)
{
	unsigned int A = prev_adler.rolling_AB.A;
	unsigned int B = prev_adler.rolling_AB.B;
	rolling_checksum_t rcksm;
	rcksm.rolling_AB.A = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
	rcksm.rolling_AB.B = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
	return rcksm;
}

/* checksum information */
typedef struct {
	unsigned int block_nr;
	rolling_checksum_t rcksm;
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	struct list_head hash;
} checksum_t;

#endif
