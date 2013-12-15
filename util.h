#ifndef __UTIL_H
#define __UTIL_H
#include <fcntl.h>

inline void set_nonblock(int fd)
{
	fcntl(fd,F_SETFL,O_NONBLOCK);
}

#endif
