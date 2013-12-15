#ifndef __RZYNC_H
#define __RZYNC_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
//#include <event2/event.h>
#include <event.h>
#include <errno.h>
#include "list_head.h"
#include "md5.h"

#define RZYNC_IP	"192.168.0.23"
#define RZYNC_PORT	19717

#define offzetof(member,type)		((unsigned int) &((type*)0)->member)

#define containerof(ptr,type,member)	({	\
		typeof(((type*)0)->member) *_fp = ptr;	\
		(type*)((unsigned char*)_fp - offzetof(member,type));	})

#define RZYNC_BLOCK_SIZE			4096
#define RZYNC_MD5_CHECK_SUM_BITS	32
#define RZYNC_MAX_NAME_LENGTH		256

/* ----------------- rolling hash ------------------ */
typedef union {
	unsigned int rolling_checksum;
	struct {
		unsigned short A;
		unsigned short B;
	} rolling_AB;
} rolling_checksum_t;

rolling_checksum_t adler32_direct(unsigned char *buf,int n);
rolling_checksum_t adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,rolling_checksum_t prev_adler);


/* ----------------- hash table ------------------ */
/* checksum information
 * CANNOT BE USED AS INTER-MACHINE TRANSMISSION FORMAT
 * ONLY FOR CONSTRUCTING IN-MEMORY HASH TABLE */
typedef struct {
	unsigned int block_nr;
	rolling_checksum_t rcksm;
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	struct list_head hash;	// for list in hash table
} checksum_t;
#define ptr_checksumof(lh)	containerof(lh,checksum_t,hash)

/* checksum hash table */
typedef struct {
	unsigned int hash_nr;
	struct list_head *slots;
} checksum_hashtable_t;

#define CHECKSUN_HASH_SLOTS_NR	4096

checksum_hashtable_t *checksum_hashtable_init(unsigned int nr);
void checksum_hashtable_destory(checksum_hashtable_t *ht);

/* ----------------- PROTOCOL SPECIFICATION ------------------ */
#define RZYNC_FILE_INFO_SIZE		512	// 512 bytes for file infomation 
#define RZYNC_CHECKSUM_HEADER_SIZE	32	// 32 bytes for checksum header
#define RZYNC_CHECKSUM_SIZE			128	// 128 bytes for each checksum

enum src_state {
	SRC_INIT = 0,	// ready to send sync request
	SRC_REQ_SENT,	// request sent
	SRC_CHKSM_HEADER_RECEIVED,	// construct hash table,ready to receive checksum
	SRC_CHKSM_ALL_RECEIVED,		// all checksums inserted into hash table
	SRC_DELTA_FILE_DONE,		// search for duplicated block, build the delta file
	SRC_DONE		// all done
};

enum dst_state {
	DST_INIT = 0,	// ready to receive sync request
	DST_REQ_RECEIVED,
	DST_CHKSM_HEADER_SENT,
	DST_CHKSM_ALL_SENT,
	DST_DELTA_FILE_RECEIVED,
	DST_DONE
};

#define RZYNC_BUF_SIZE	(1<<14)	// 16KB for client buffer
/* ----------------- src side struct ------------------ */
typedef struct {
	char filename[RZYNC_MAX_NAME_LENGTH];	
	unsigned long long size;	// file size in bytes
	long long mtime;	// modification time of the file from the src side
	int filefd;	// file fd
	int sockfd;	// socket to read and write
	enum src_state state;	// current state
	checksum_hashtable_t *hashtable;
	int length;	// total length in the buffer
	int offset;	// current offset in the buffer
	char buf[RZYNC_BUF_SIZE];
} rzync_src_t;
/* ----------------- dest side struct ------------------ */
typedef struct {
	char filename[RZYNC_MAX_NAME_LENGTH];	
	unsigned long long size;	// file size in bytes
	long long mtime;	// modification time of the file from the src side
	int filefd;	// file fd
	int dstfd;	// the file to be written
	struct event ev_read;
	struct event ev_write;
	int sockfd;	// socket to read and write
	enum dst_state state;	// current state
	struct list_head flist;	// for the free list
	int length;	// total length in the buffer
	int offset;	// current offset in the buffer
	char buf[RZYNC_BUF_SIZE];
} rzync_dst_t;
#define ptr_clientof(lh)	containerof(lh,rzync_dst_t,flist)

typedef struct rzyncdst_pool_t {
	unsigned int client_nr;
	rzync_dst_t *clients;
	struct rzyncdst_pool_t *next;
} rzyncdst_pool_t;

typedef struct {
	unsigned int client_nr;
	struct list_head free_list;
	rzyncdst_pool_t *pool_head;
} rzyncdst_freelist_t;

#define RZYNC_CLIENT_POOL_SIZE	1024	// default client pool size

rzyncdst_freelist_t *rzyncdst_freelist_init(void);
void rzyncdst_freelist_destory(rzyncdst_freelist_t *fl);
rzync_dst_t *get_rzyncdst(rzyncdst_freelist_t *fl);
void put_rzyncdst(rzyncdst_freelist_t *fl,rzync_dst_t *cl);

#endif

