#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include "md5.h"
#include "adler32.h"

#define BLOCK_SZ		(1<<10)
#define BLOCK_MIN_SZ	(1<<9)
#define BLOCK_MAX_SZ	(1<<11)
#define BLOCK_WIN_SZ	48

#define MIN_BLK_SZ	(BLOCK_MIN_SZ-BLOCK_WIN_SZ)

#define BUF_MAX_SZ		(1<<15)

#define CHUNK_CDC_D		BLOCK_SZ
#define CHUNK_CDC_R		13

#define CDC_MD5_LEN		32

/* content-defined chunking */
static int file_chunk_cdc(int fd_src)
{
	char buf[BUF_MAX_SZ] = {0};
	char block_buf[BLOCK_MAX_SZ] = {0};
	char win_buf[BLOCK_WIN_SZ + 1] = {0};
	unsigned char md5_checksum[CDC_MD5_LEN+1] = {0};
	unsigned int bpos = 0;
	unsigned int rwsize = 0;
	unsigned int exp_rwsize = BUF_MAX_SZ;
	unsigned int head, tail;
	unsigned int block_sz = 0, old_block_sz = 0;
	unsigned int hkey = 0;
	unsigned long long offset = 0;

	while(rwsize = read(fd_src, buf + bpos, exp_rwsize)) {
		/* last chunk */
		if ((rwsize + bpos + block_sz) < BLOCK_MIN_SZ)
			break;

		head = 0;
		tail = bpos + rwsize;
		/* avoid unnecessary computation and comparsion */
		if (block_sz < MIN_BLK_SZ) {
			old_block_sz = block_sz;
			unsigned int the_big_blk_sz = block_sz + tail - head;
			block_sz = (the_big_blk_sz > MIN_BLK_SZ) ?
				MIN_BLK_SZ : the_big_blk_sz;
			unsigned int bytes_cpd = block_sz - old_block_sz;
			memcpy(block_buf + old_block_sz, buf + head, bytes_cpd);
			head += bytes_cpd;
		}

		while ((head + BLOCK_WIN_SZ) <= tail) {
			memcpy(win_buf, buf + head, BLOCK_WIN_SZ);
			hkey = (block_sz == MIN_BLK_SZ) ? adler32_checksum(win_buf, BLOCK_WIN_SZ) :
				adler32_rolling_checksum(hkey, BLOCK_WIN_SZ, buf[head-1], buf[head+BLOCK_WIN_SZ-1]);

			/* get a normal chunk, write block info to chunk file */
			if ((hkey % BLOCK_SZ) == CHUNK_CDC_R) {
				memcpy(block_buf + block_sz, buf + head, BLOCK_WIN_SZ);
				head += BLOCK_WIN_SZ;
				block_sz += BLOCK_WIN_SZ;
				if (block_sz >= BLOCK_MIN_SZ) {
					goto blk_found;
				}
			} else {
				block_buf[block_sz++] = buf[head++];
				/* get an abnormal chunk, write block info to chunk file */
				if (block_sz >= BLOCK_MAX_SZ) {
					goto blk_found;
				}
			}
			assert(block_sz != 0);
			continue;
blk_found:
			md5s_of_str(block_buf, block_sz, md5_checksum);
			md5_checksum[CDC_MD5_LEN] = '\0';
			printf("%s\n",md5_checksum);
			offset += block_sz;
			block_sz = 0;

			/* avoid unnecessary computation and comparsion */
			unsigned int bytes_left = tail - head;
			block_sz = (bytes_left > MIN_BLK_SZ) ?
				MIN_BLK_SZ : bytes_left;
			memcpy(block_buf, buf + head, block_sz);
			head += block_sz;
		}

		/* read expected data from file to full up buf */
		bpos = tail - head;
		exp_rwsize = BUF_MAX_SZ - bpos;
		memmove(buf, buf + head, bpos);
	}

	if (rwsize == -1)
		return -1;
	
	/* process last block */
	unsigned int last_block_sz = ((rwsize + bpos + block_sz) >= 0) ? rwsize + bpos + block_sz : 0;
	char last_block_buf[BLOCK_MAX_SZ] = {0};
	if (last_block_sz > 0) {
		memcpy(last_block_buf, block_buf, block_sz);
		memcpy(last_block_buf + block_sz, buf, rwsize + bpos);
		md5s_of_str(last_block_buf, last_block_sz, md5_checksum);
		md5_checksum[CDC_MD5_LEN] = '\0';
		printf("%s\n",md5_checksum);
	}

	return 0;
}

int main(int argc,char* argv[])
{
	if(argc != 2) {
		fprintf(stderr,"Usage : ./cdc <src_file>\n");
		return 1;
	}
	char* filename = argv[1];

	int fd = open(filename,O_RDONLY);
	if(fd < 0) {
		perror("open");
		return 1;
	}

	file_cdc(fd);

	close(fd);

	return 0;
}

