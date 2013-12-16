#include "rzync.h"

/* ----------------- rolling hash implementation ------------------ */
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

#define ROLLING_EQUAL(x,y)	(x.rolling_checksum == y.rolling_checksum)

/* direct calculation of adler32 */
rolling_checksum_t adler32_direct(unsigned char *buf,int n)
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
rolling_checksum_t adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,rolling_checksum_t prev_adler)
{
	unsigned int A = prev_adler.rolling_AB.A;
	unsigned int B = prev_adler.rolling_AB.B;
	rolling_checksum_t rcksm;
	rcksm.rolling_AB.A = (A + new_ch + ADLER_MOD - old_ch) % ADLER_MOD;
	rcksm.rolling_AB.B = (B + A + new_ch + ADLER_MOD - 1 - (n + 1) * old_ch) % ADLER_MOD;
	return rcksm;
}

/* ----------------- checksum hashtable ------------------ */
/* checksum_hashtable_init :
 * initialize a hash table with 'hash_nr' slots */
checksum_hashtable_t *checksum_hashtable_init(unsigned int hash_nr)
{
	checksum_hashtable_t *ht = (checksum_hashtable_t*)malloc(sizeof(checksum_hashtable_t));
	if(!ht) {
		perror("malloc for checksum_hashtable_t");
		return NULL;
	}
	ht->hash_nr = hash_nr;
	ht->slots = (struct list_head*)malloc(hash_nr*sizeof(struct list_head));
	if(!ht->slots) {
		perror("malloc for hash slots");
		free(ht);
		return NULL;
	}

	int i;
	for(i=0;i<hash_nr;i++) {
		list_head_init(&ht->slots[i]);
	}
	return ht;
}

void checksum_hashtable_destory(checksum_hashtable_t *ht)
{
	if(!ht) {
		return;
	}
	if(ht->slots) {
		free(ht->slots);
	}
	free(ht);
}

/* ----------------- rzync_dst_t pool ------------------ */
/* initialize to a null list */
rzyncdst_freelist_t *rzyncdst_freelist_init(void)
{
	rzyncdst_freelist_t *fl = (rzyncdst_freelist_t*)malloc(sizeof(rzyncdst_freelist_t));
	if(!fl) {
		return NULL;
	}
	fl->client_nr = 0;
	fl->pool_head = NULL;
	list_head_init(&fl->free_list);
	return fl;
}

void rzyncdst_freelist_destory(rzyncdst_freelist_t *fl)
{
	rzyncdst_pool_t *p = fl->pool_head;
	while(p) {
		rzyncdst_pool_t *q = p->next;
		if(p->clients) {
			free(p->clients);
		}
		free(p);
		p = q;
	}
	free(fl);
	printf("free list destroyed...........\n");
}

rzync_dst_t *get_rzyncdst(rzyncdst_freelist_t *fl)
{
	rzync_dst_t *cl;
//	printf("current in the pool --------- %d\n",fl->client_nr);
	if(fl->client_nr == 0) {
//		printf("need add some rzync_dst_t in the pool........\n");
		/* add more elements */
		rzyncdst_pool_t *cp = (rzyncdst_pool_t*)malloc(sizeof(rzyncdst_pool_t));
		if(!cp) {
			return NULL;
		}
		cp->client_nr = RZYNC_CLIENT_POOL_SIZE;
		cp->next = NULL;
		cp->clients = (rzync_dst_t*)malloc(cp->client_nr*sizeof(rzync_dst_t));
		if(!cp->clients) {
			free(cp);
			return NULL;
		}
		/* insert to free list */
		int i;
		for(i=0;i<cp->client_nr-1;i++) {
			list_add(&cp->clients[i].flist,&fl->free_list);
		}
		/* insert to pool list */
		if(!fl->pool_head) {
			fl->pool_head = cp;
		}else {
			cp->next = fl->pool_head->next;
			fl->pool_head = cp;
		}
		fl->client_nr += (cp->client_nr - 1);
		/* return the last one */
		cl = &cp->clients[cp->client_nr-1];
	} else {
		/* get one directly from free list */
		struct list_head *l = fl->free_list.next;
		list_del(l);
		fl->client_nr--;
		cl = ptr_clientof(l);
	}
//	printf("after get one from the pool --------- %d\n",fl->client_nr);
	return cl;
}

void put_rzyncdst(rzyncdst_freelist_t *fl,rzync_dst_t *cl)
{
	list_add(&cl->flist,&fl->free_list);
	fl->client_nr++;
//	printf("after put one to the pool, in the pool now --------- %d\n",fl->client_nr);
}

