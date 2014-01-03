#ifndef _CDC_H
#define _CDC_H

#define BLOCK_SZ		(1<<10)
#define BLOCK_MIN_SZ	(1<<9)
#define BLOCK_MAX_SZ	(1<<11)
#define BLOCK_WIN_SZ	48

#define MIN_BLK_SZ	(BLOCK_MIN_SZ-BLOCK_WIN_SZ)

#define BUF_MAX_SZ		(1<<15)

#define CHUNK_CDC_D		BLOCK_SZ
#define CHUNK_CDC_R		13

#define CDC_MD5_LEN		32

enum {
	CDC_OK = 0,
	CDC_ERR
};
int file_cdc(char* filename,char* cdc_file,int* block_nr_p);

#endif
