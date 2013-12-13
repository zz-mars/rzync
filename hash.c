#include"global.h"
#include"hash.h"

/* A Simple Hash Function */
u32 simple_hash(char *str)
{
	register u32 hash;
	register u8 *p;

	for(hash = 0, p = (u8 *)str; *p ; p++)
		hash = 31 * hash + *p;

	return (hash & 0x7FFFFFFF);
}

/* RS Hash Function */
u32 RS_hash(char *str)
{
	u32 b = 378551;
	u32 a = 63689;
	u32 hash = 0;

	while (*str){
		hash = hash * a + (*str++);
		a *= b;
	}

	return (hash & 0x7FFFFFFF);
}

/* JS Hash Function */
u32 JS_hash(char *str)
{
	u32 hash = 1315423911;

	while (*str){
		hash ^= ((hash << 5) + (*str++) + (hash >> 2));
	}

	return (hash & 0x7FFFFFFF);
}

/* P. J. Weinberger Hash Function */
u32 PJW_hash(char *str)
{
	u32 BitsInUnignedInt = (u32)(sizeof(u32) * 8);
	u32 ThreeQuarters = (u32)((BitsInUnignedInt   * 3) / 4);
	u32 OneEighth = (u32)(BitsInUnignedInt / 8);

	u32 HighBits = (u32)(0xFFFFFFFF) << (BitsInUnignedInt - OneEighth);
	u32 hash = 0;
	u32 test = 0;

	while (*str){
		hash = (hash << OneEighth) + (*str++);
		if ((test = hash & HighBits) != 0){
			hash = ((hash ^ (test >> ThreeQuarters)) & (~HighBits));
		}
	}

	return (hash & 0x7FFFFFFF);
}

/* ELF Hash Function */
u32 ELF_hash(char *str)
{
	u32 hash = 0;
	u32 x    = 0;

	while (*str){
		hash = (hash << 4) + (*str++);
		if ((x = hash & 0xF0000000L) != 0){
			hash ^= (x >> 24);
			hash &= ~x;
		}
	}

	return (hash & 0x7FFFFFFF);
}

/* BKDR Hash Function */
u32 BKDR_hash(char *str)
{
	u32 seed = 131; // 31 131 1313 13131 131313 etc..
	u32 hash = 0;

	while (*str){
		hash = hash * seed + (*str++);
	}

	return (hash & 0x7FFFFFFF);
}

/* SDBM Hash Function */
u32 SDBM_hash(char *str)
{
	u32 hash = 0;

	while (*str){
		hash = (*str++) + (hash << 6) + (hash << 16) - hash;
	}

	return (hash & 0x7FFFFFFF);
}

/* DJB Hash Function */
u32 DJB_hash(char *str)
{
	u32 hash = 5381;

	while (*str){
		hash += (hash << 5) + (*str++);
	}

	return (hash & 0x7FFFFFFF);
}

/* AP Hash Function */
u32 AP_hash(char *str)
{
	u32 hash = 0;
	int i;
	for (i=0; *str; i++){
		if ((i & 1) == 0){
			hash ^= ((hash << 7) ^ (*str++) ^ (hash >> 3));
		}else{
			hash ^= (~((hash << 11) ^ (*str++) ^ (hash >> 5)));
		}
	}

	return (hash & 0x7FFFFFFF);
}

/* CRC Hash Function */
u32 CRC_hash(char *str)
{
	u32 nleft = strlen(str);
	u64 sum = 0;
	u16 *w = (u16 *)str;
	u16 answer = 0;

	/*
	 * Our algorithm is simple, using a 32 bit accumulator (sum), we add
	 * sequential 16 bit words to it, and at the end, fold back all the
	 * carry bits from the top 16 bits into the lower 16 bits.
	 */
	while ( nleft > 1 ) {
		sum += *w++;
		nleft -= 2;
	}
	/*
	 * mop up an odd byte, if necessary
	 */
	if ( 1 == nleft ) {
		*( u8 * )( &answer ) = *( u8 * )w ;
		sum += answer;
	}
	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 * add hi 16 to low 16
	 */
	sum = ( sum >> 16 ) + ( sum & 0xFFFF );
	/* add carry */
	sum += ( sum >> 16 );
	/* truncate to 16 bits */
	answer = ~sum;

	return (answer & 0xFFFFFFFF);
}


