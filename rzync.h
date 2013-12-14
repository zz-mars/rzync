#ifndef __RZYNC_H
#define __RZYNC_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "list_head.h"
#include "md5.h"

#define RZYNC_BLOCK_SIZE			4096
#define RZYNC_MD5_CHECK_SUM_BITS	32
#define RZYNC_MAX_NAME_LENGTH		256

typedef union {
	unsigned int rolling_checksum;
	struct {
		unsigned short A;
		unsigned short B;
	} rolling_AB;
} rolling_checksum_t;

rolling_checksum_t adler32_direct(unsigned char *buf,int n);
rolling_checksum_t adler32_rolling(unsigned char old_ch,unsigned char new_ch,int n,rolling_checksum_t prev_adler);


/* checksum information
 * CANNOT BE USED AS INTER-MACHINE TRANSMISSION FORMAT
 * ONLY FOR CONSTRUCTING IN-MEMORY HASH TABLE */
typedef struct {
	unsigned int block_nr;
	rolling_checksum_t rcksm;
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	struct list_head hash;
} checksum_t;

/* checksum hash table */
typedef struct {
	unsigned int hash_nr;
	struct list_head *slots;
} checksum_hashtable_t;

checksum_hashtable_t *checksum_hashtable_init(unsigned int nr);
void checksum_hashtable_destory(checksum_hashtable_t *ht);


#define RZYNC_CLIENT_BUF_SIZE	(1<<14)
/* for each client */
typedef struct {
	char filename[RZYNC_MAX_NAME_LENGTH];	// file name length
	unsigned long long size;		// file size in bytes
	time_t mtime;			// modification time of the file from the src side
	int sockfd;
	char buf[];
} rzync_client_t;

typedef struct {
	rzync_client_t client;
	checksum_hashtable_t *hashtable;// for fast indexing of checksum
} rzync_file_info_t;

typedef struct {
} rzync_file_info_pool_t;

#endif
