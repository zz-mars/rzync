#include "rzync.h"
#include "util.h"

enum {
	CALCULATE_DELTA_OK = 0,
	CALCULATE_DELTA_NO_MORE_CAL_NEEDED,
	CALCULATE_DELTA_ERR
};
int calculate_delta(rzync_src_t *src)
{
	unsigned int in_buf_not_processed = 
		src->src_delta.buf.length - src->src_delta.buf.offset;
	assert(in_buf_not_processed > 0);
	if(in_buf_not_processed >= src->checksum_header.block_sz) {
		/* enough data to process,
		 * directly go to calculate delta */
		goto calculate_delta;
	}
	/* else try to read more data from file */
	if(in_buf_not_processed > 0) {
		/* If there're some data in the buffer,
		 * move them to the beginning of the buffer 
		 * Use tmp buf to avoid overlapping of the move operation */
		char *tmp_buf = (char*)malloc(in_buf_not_processed+1);
		if(!tmp_buf) {
			return CALCULATE_DELTA_ERR;
		}
		/* copy to tmp buffer */
		memcpy(tmp_buf,
				src->src_delta.buf.buf+src->src_delta.buf.offset,
				in_buf_not_processed);
		/* clear buffer */
		memset(src->src_delta.buf.buf,0,RZYNC_DETLTA_BUF_SIZE);
		/* copy back */
		memcpy(src->src_delta.buf.buf,tmp_buf,in_buf_not_processed);
		free(tmp_buf);
	}else if(in_buf_not_processed == 0){
		/* simply clear the buffer */
		memset(src->src_delta.buf.buf,0,RZYNC_DETLTA_BUF_SIZE);
	}
	/* re-set offset and length */
	src->src_delta.buf.length = in_buf_not_processed;
	src->src_delta.buf.offset = 0;
	/* read more from file */
	unsigned int bytes_in_file_not_processed = 
		src->size - src->src_delta.offset;
	if(bytes_in_file_not_processed == 0) {
		/* no more data to read from file,
		 * calculate from the data currently in buffer */
		goto calculate_delta;
	}
	assert(bytes_in_file_not_processed > 0);
	/* bytes_in_file_not_processed > 0 */
	unsigned int buf_capcity = RZYNC_DETLTA_BUF_SIZE - src->src_delta.buf.length;
	unsigned int to_read = 
		buf_capcity<bytes_in_file_not_processed?buf_capcity:bytes_in_file_not_processed;
	int already_read = 0;
	/* make sure */
	while(already_read != to_read) {
		int n = read(src->filefd,
				src->src_delta.buf.buf+in_buf_not_processed+already_read,
				to_read-already_read);
		if(n < 0) {
			if(errno == EINTR) {
				continue;
			}
			/* unrecoverable error */
			return CALCULATE_DELTA_ERR;
		}
		already_read += n;
	}
	/* update some state */
	src->src_delta.offset += already_read;
	src->src_delta.buf.length += already_read;
calculate_delta:
	in_buf_not_processed = 
		src->src_delta.buf.length - src->src_delta.buf.offset;
	if(in_buf_not_processed == 0) {
		/* NO MORE DATA TO PROCESS */
		return CALCULATE_DELTA_NO_MORE_CAL_NEEDED;
	}
	assert(in_buf_not_processed > 0);
	/* clear socket buf */
	memset(src->buf,0,RZYNC_BUF_SIZE);
	if(in_buf_not_processed < src->checksum_header.block_nr) {
		/* the last block to process, no need to calculate */
		snprintf(src->buf,RZYNC_BUF_SIZE,"$%c$%u\n",DELTA_NDUP,in_buf_not_processed);
		int delta_header_len = strlen(src->buf);
		memcpy(src->buf+delta_header_len,
				src->src_delta.buf.buf+src->src_delta.buf.offset,
				in_buf_not_processed);
		src->length = delta_header_len + in_buf_not_processed;
		return CALCULATE_DELTA_OK;
	}
	/* the real challenge comes here... */
}

int send_delta(rzync_src_t *src)
{
}

