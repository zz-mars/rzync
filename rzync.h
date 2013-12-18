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
	char md5[RZYNC_MD5_CHECK_SUM_BITS+1];
	struct list_head hash;	// for list in hash table
} checksum_t;
#define ptr_checksumof(lh)	containerof(lh,checksum_t,hash)

/* checksum hash table */
typedef struct {
	unsigned int hash_bits;
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
	SRC_CALCULATING_DELTA,		// calculate delta file
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
#define RZYNC_DETLTA_BUF_SIZE	(1<<14)	// 16KB for src file buffer
#define TMP_FILE_NAME_LEN	33
/* ----------------- src side struct ------------------ */
typedef struct {
	char filename[RZYNC_MAX_NAME_LENGTH];	
	unsigned long long size;	// file size in bytes
	long long mtime;	// modification time of the file from the src side
	int filefd;	// file fd
	int sockfd;	// socket to read and write
	unsigned char state;	// current state
	struct {
		unsigned int block_nr;
		unsigned int block_sz;
	} checksum_header;
	/* src delta */
	struct {
		unsigned long long offset;	// bytes already processed
		checksum_t chksm;			// checksum
		struct {
			unsigned int offset;	// current offset in buf
			unsigned int length;	// total length in buf
			char buf[RZYNC_DETLTA_BUF_SIZE];
		} buf;
	} src_delta;
	unsigned int checksum_recvd;
	/* for building checksum hash table */
	checksum_t *checksums;	// checksums
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
	struct {
		int fd;
		unsigned int block_nr;	// total block number 
		unsigned int block_sz;	// each block size
		unsigned int checksum_sent;	// already sent
	} dst_local_file;
	struct {
		int fd;
		char tmp_filename[TMP_FILE_NAME_LEN];
		unsigned long long bytes_recvd;
	} dst_sync_file;
	int dstfd;	// the file to be written
	struct event ev_read;
	struct event ev_write;
	int sockfd;	// socket to read and write
	unsigned char state;	// current state
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

/* ----------------- PROTOCOL SPECIFICATION ------------------ */
/*
#define RZYNC_FILE_INFO_SIZE		512	// 512 bytes for file infomation buffer
#define RZYNC_CHECKSUM_HEADER_SIZE	32	// 32 bytes for checksum header
#define RZYNC_CHECKSUM_SIZE			64	// 128 bytes for each checksum
*/

/* 1) The destination of the synchronization listens on a specific port
 * 2) The source side of the synchronization sends the information of 
 *	  the file to be synchronized to dst. 
 *	  The information to be sent : <name,size,modification time>
 *	  Use ASCII string as the information format.
 *	  Format specification :
 *   ---------------------------------
 *	  #length_of_file_name\n
 *	  $filename\n
 *	  $size\n
 *	  $modification_time\n
 *   ---------------------------------
 * 3) Once the dst side receives this information, it checks file specified 
 *   by the request in its current working directory. Get the size then send 
 *   the total block number and block size to the src as the checksum Header.
 *   Format specification :
 *   ---------------------------------
 *   $block_nr\n
 *   $block_size\n
 *   == post-script ==
 *   If no file with name  "$filename" is found, the dst will just send the 
 *   checksum header with block_nr = 0, so that the src side will send all 
 *   its file data without delta-encoding.
 *   ---------------------------------
 * 4) After sending the checksum header, it calculates the rolling hash and
 *   md5 of the block of local file, send the checksum information to src 
 *   side in the following format:
 *   ---------------------------------
 *   $block_num\n
 *   $rolling_checksum.A\n
 *   $rolling_checksum.B\n
 *   $md5\n
 *   ---------------------------------
 * 5) The src side receives these checksum infomation, keep them in a hash 
 *   table in memory. The src side scans the file to be synced, do the delta
 *   encoding.
 * */

#endif

