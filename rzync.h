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

/* checksum information */
typedef struct {
	unsigned int block_nr;
	char rolling_checksum[RZYNC_ROLLING_HASH_BITS];
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	struct list_head hash;
} checksum_t;

#endif
