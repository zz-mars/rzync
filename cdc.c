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
		if (block_sz < (BLOCK_MIN_SZ - BLOCK_WIN_SZ)) {
			old_block_sz = block_sz;
			block_sz = ((block_sz + tail - head) > (BLOCK_MIN_SZ - BLOCK_WIN_SZ)) ?
				BLOCK_MIN_SZ - BLOCK_WIN_SZ : block_sz + tail -head;
			memcpy(block_buf + old_block_sz, buf + head, block_sz - old_block_sz);
			head += (block_sz - old_block_sz);
		}

		while ((head + BLOCK_WIN_SZ) <= tail) {
			memcpy(win_buf, buf + head, BLOCK_WIN_SZ);
			hkey = (block_sz == (BLOCK_MIN_SZ - BLOCK_WIN_SZ)) ? adler32_checksum(win_buf, BLOCK_WIN_SZ) :
				adler32_rolling_checksum(hkey, BLOCK_WIN_SZ, buf[head-1], buf[head+BLOCK_WIN_SZ-1]);

			/* get a normal chunk, write block info to chunk file */
			if ((hkey % BLOCK_SZ) == CHUNK_CDC_R) {
				memcpy(block_buf + block_sz, buf + head, BLOCK_WIN_SZ);
				head += BLOCK_WIN_SZ;
				block_sz += BLOCK_WIN_SZ;
				if (block_sz >= BLOCK_MIN_SZ) {
					md5s_of_str(block_buf, block_sz, md5_checksum);
					md5_checksum[CDC_MD5_LEN] = '\0';
					printf("%s\n",md5_checksum);
					offset += block_sz;
					block_sz = 0;
				}
			} else {
				block_buf[block_sz++] = buf[head++];
				/* get an abnormal chunk, write block info to chunk file */
				if (block_sz >= BLOCK_MAX_SZ) {
					md5s_of_str(block_buf, block_sz, md5_checksum);
					md5_checksum[CDC_MD5_LEN] = '\0';
					printf("%s\n",md5_checksum);
					offset += block_sz;
					block_sz = 0;
				}
			}

			/* avoid unnecessary computation and comparsion */
			if (block_sz == 0) {
				block_sz = ((tail - head) > (BLOCK_MIN_SZ - BLOCK_WIN_SZ)) ?
					BLOCK_MIN_SZ - BLOCK_WIN_SZ : tail - head;
				memcpy(block_buf, buf + head, block_sz);
				head = ((tail - head) > (BLOCK_MIN_SZ - BLOCK_WIN_SZ)) ?
					head + (BLOCK_MIN_SZ - BLOCK_WIN_SZ) : tail;
			}
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

enum {
	CDC_OK = 0,
	CDC_ERR
};

int file_cdc(int fd)
{
	unsigned char block_buf[BLOCK_MAX_SZ];
	unsigned char wbuf[BLOCK_WIN_SZ];
	unsigned char md5[CDC_MD5_LEN+1];

	int ret = CDC_ERR;

	unsigned int block_sz = 0;
	unsigned int block_nr = 0;	// how many blocks

	unsigned char buf[BUF_MAX_SZ];
	unsigned int exp_rwsize = BUF_MAX_SZ;
	unsigned int bpos = 0;
	unsigned int rwsize = 0;
	unsigned long long offset = 0;
	while((rwsize = read(fd,buf+bpos,exp_rwsize)) > 0) {
		/* last block */
		if((block_sz + bpos + rwsize) < BLOCK_MIN_SZ) {
			break;
		}
		unsigned int head = 0;
		unsigned int tail = bpos + rwsize;
		if(block_sz < MIN_BLK_SZ) {
			unsigned int old_block_sz = block_sz;
			unsigned int bytes_in_buf = tail - head;
			block_sz = ((block_sz + bytes_in_buf) > MIN_BLK_SZ) ?
				MIN_BLK_SZ:(block_sz + bytes_in_buf);
			unsigned bytes_cped = block_sz - old_block_sz;
			memcpy(block_buf+old_block_sz,buf+head,bytes_cped);
			head += bytes_cped;
		}
		/* sliding! */
		unsigned int hkey = 0;
		while((head + BLOCK_WIN_SZ) <= tail) {
			memcpy(wbuf,buf+head,BLOCK_WIN_SZ);
			hkey = (block_sz == MIN_BLK_SZ) ? adler32_checksum(wbuf,BLOCK_WIN_SZ) : 
				adler32_rolling_checksum(hkey,BLOCK_WIN_SZ,buf[head-1],buf[head+BLOCK_WIN_SZ-1]);
			if((hkey % CHUNK_CDC_D) == CHUNK_CDC_R) {
				memcpy(block_buf+block_sz,buf+head,BLOCK_WIN_SZ);
				head += BLOCK_WIN_SZ;
				block_sz += BLOCK_WIN_SZ;
				if(block_sz >= BLOCK_MIN_SZ) {
					goto blk_found;
				}
			} else {
				/* one byte forward */
				block_buf[block_sz++] = buf[head++];
				assert(block_sz <= BLOCK_MAX_SZ);
				if(block_sz == BLOCK_MAX_SZ) {
					goto blk_found;
				}
			}
			continue;
blk_found:
			block_nr++;
			md5s_of_str(block_buf,block_sz,md5);
			md5[CDC_MD5_LEN] = '\0';
			printf("%s\n",md5);

			offset += block_sz;
			unsigned int bytes_in_buf = tail - head;
			block_sz = (bytes_in_buf > MIN_BLK_SZ) ?
				MIN_BLK_SZ:bytes_in_buf;
			memcpy(block_buf,buf+head,block_sz);
			head += block_sz;
		}
		/* try to read more */
		assert(tail >= head);
		bpos = tail - head;
		exp_rwsize = BUF_MAX_SZ  - bpos;
		memmove(buf,buf+head,bpos);
	}

	/* last block */
	int bytes_left = block_sz + bpos + rwsize;
	unsigned int last_blk_sz = bytes_left > 0?bytes_left:0;
	if(last_blk_sz > 0) {
		block_nr++;
		memcpy(block_buf+block_sz,buf,bpos+rwsize);
		md5s_of_str(block_buf,last_blk_sz,md5);
		md5[CDC_MD5_LEN] = '\0';
		printf("%s\n",md5);
	}

	ret = CDC_OK;
close_fd:
	close(fd);
rt:
	return ret;
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

	file_chunk_cdc(fd);

	close(fd);

	return 0;
}

