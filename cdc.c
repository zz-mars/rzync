#include "cdc.h"
#include "md5.h"
#include <stdio.h>
#include <fcntl.h>

int file_cdc(char* filename,char* cdc_filename,int* block_nr_p)
{
	unsigned char buf[BUF_MAX_SZ];
	unsigned char block_buf[BLOCK_MAX_SZ];
	unsigned char wbuf[BLOCK_WIN_SZ];
	unsigned int adler32_checksum;

	unsigned int bpos = 0;
	unsigned int rwsize = 0;

	unsigned int block_sz = 0,old_block_sz = 0;

	unsigned long long offset = 0;

	unsigned int block_nr = 0;	// how many blocks

	int ret = CDC_ERR;

	int fd = open(filename,O_RDONLY);
	if(fd < 0) {
		perror("open file");
		goto rt;
	}

	int cdcfd = open(cdc_filename,O_TRUNC | O_CREAT | O_WRONLY,0660);
	if(cdcfd < 0) {
		perror("open cdc file");
		goto close_fd;
	}

	unsigned int exp_rwsize = BUF_MAX_SZ;
	while(rwsize = read(fd,buf+bpos,exp_rwsize)) {
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
		unsigned char md5[CDC_MD5_LEN+1];
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
			md5[CDC_MD5_LEN] = '\n';
			/* write to file */
			if(write(cdcfd,md5,CDC_MD5_LEN+1) != (CDC_MD5_LEN+1)) {
				goto close(fd);
			}
			offset += block_sz;
			unsigned int bytes_in_buf = tail - head;
			block_sz = (bytes_in_buf > MIN_BLK_SZ) ?
				MIN_BLK_SZ:bytes_in_buf;
			memcpy(block_buf,buf+head,block_sz);
			head += block_sz;
		}
		/* try to read more */
		bpos = tail - head;
		exp_rwsize = BUF_MAX_SZ  - bpos;
		memmove(buf,buf+head,bpos);
	}

	/* last block */
	*block_nr_p = block_nr;
	ret = CDC_OK;
close_fd:
	close(fd);
rt:
	return ret;
}

