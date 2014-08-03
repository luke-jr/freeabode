#ifndef FABD_UTIL_H
#define FABD_UTIL_H

#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

enum fabd_tristate {
	FTS_FALSE = (int)false,
	FTS_TRUE  = (int)true,
	FTS_UNKNOWN,
};

extern void bin2hex(char *, const void *, size_t);

static inline
int timespec_cmp(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec == b->tv_sec)
		return a->tv_nsec - b->tv_nsec;
	return a->tv_sec - b->tv_sec;
}

static inline
void timespec_add(const struct timespec *a, const struct timespec *b, struct timespec *result)
{
	result->tv_sec = a->tv_sec + b->tv_sec;
	result->tv_nsec = a->tv_nsec + b->tv_nsec;
	if (result->tv_nsec >= 1000000000)
	{
		++result->tv_sec;
		result->tv_nsec -= 1000000000;
	}
}

#endif
