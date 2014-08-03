#ifndef FABD_UTIL_H
#define FABD_UTIL_H

#include <stdbool.h>
#include <sys/types.h>

enum fabd_tristate {
	FTS_FALSE = (int)false,
	FTS_TRUE  = (int)true,
	FTS_UNKNOWN,
};

extern void bin2hex(char *, const void *, size_t);

#endif
