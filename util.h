#ifndef __UTIL_H
#define __UTIL_H
#include <fcntl.h>

inline void set_nonblock(int fd)
{
	fcntl(fd,F_SETFL,O_NONBLOCK);
}

#define STR2I_PARSE_FAIL	-1
inline int str2i(char **pp,char start_pat,char end_pat)
{
	char *p = *pp;
	if(*p++ != start_pat) {
		fprintf(stderr,"illegal format!\n");
		return STR2I_PARSE_FAIL;
	}

	int r = 0;
	while(*p != end_pat) {
		if(*p < '0' || *p > '9') {
			fprintf(stderr,"illegal format!\n");
			return STR2I_PARSE_FAIL;
		}
		r *= 10;
		r += (*p++ - '0');
	}
	*pp = p+1;
	return r;
}

#define STR2LL_PARSE_FAIL	-1
inline long long str2ll(char **pp,char start_pat,char end_pat)
{
	char *p = *pp;
	if(*p++ != start_pat) {
		fprintf(stderr,"illegal format!\n");
		return STR2LL_PARSE_FAIL;
	}

	long long r = 0;
	while(*p != end_pat) {
		if(*p < '0' || *p > '9') {
			fprintf(stderr,"illegal format!\n");
			return STR2LL_PARSE_FAIL;
		}
		r *= 10;
		r += (*p++ - '0');
	}
	*pp = p+1;
	return r;
}

#endif
