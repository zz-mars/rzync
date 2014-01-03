#ifndef _ADLER32_H
#define _ADLER32_H

unsigned int adler32_checksum(unsigned char* buf,int len);
unsigned int adler32_rolling_checksum(unsigned int csum,int len,unsigned char c1,unsigned char c2);

#endif
