#ifndef FABD_UTIL_H
#define FABD_UTIL_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#define ullabs(n)  (((n) < 0) ? (ULLONG_MAX - (unsigned long long)(n) + 1) : (unsigned long long)(n))

enum fabd_tristate {
	FTS_FALSE = (int)false,
	FTS_TRUE  = (int)true,
	FTS_UNKNOWN,
};

extern void bin2hex(char *, const void *, size_t);
extern bool hex2bin(unsigned char *, const char *, size_t);

extern bool fabd_strtobool(const char *, char **endptr);

#define TIMESPEC_INIT_CLEAR  { .tv_sec = (time_t)-1 }

static inline
int timespec_cmp(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec == b->tv_sec)
		return a->tv_nsec - b->tv_nsec;
	return a->tv_sec - b->tv_sec;
}

static inline
bool timespec_isset(const struct timespec *timer)
{
	return timer->tv_sec != (time_t)-1;
}

static inline
void timespec_clear(struct timespec *timer)
{
	timer->tv_sec = (time_t)-1;
}

static inline
void timespec_min(const struct timespec *a, const struct timespec *b, struct timespec *result)
{
	if (timespec_isset(a) && timespec_cmp(a, b) < 0)
		*result = *a;
	else
		*result = *b;
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

static inline
void timespec_add_ms(const struct timespec *a, const unsigned long b_ms, struct timespec *result)
{
	struct timespec b = {
		.tv_sec = b_ms / 1000,
		.tv_nsec = (long)(b_ms % 1000) * 1000000,
	};
	timespec_add(a, &b, result);
}

static inline
void timespec_sub(const struct timespec *a, const struct timespec *b, struct timespec *result)
{
	result->tv_sec = a->tv_sec - b->tv_sec;
	result->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (result->tv_nsec < 0)
	{
		--result->tv_sec;
		result->tv_nsec += 1000000000;
	}
}

static inline
bool timespec_passed(const struct timespec *timer, const struct timespec *now, struct timespec *timeout)
{
	if (!timespec_isset(timer))
		return false;
	if (timespec_cmp(timer, now) < 0)
		return true;
	if (timeout)
		timespec_min(timeout, timer, timeout);
	return false;
}

static inline
long timespec_to_timeout_ms(const struct timespec *now, const struct timespec *timeout)
{
	if (!timespec_isset(timeout))
		return -1;
	struct timespec timeleft;
	timespec_sub(timeout, now, &timeleft);
	return ((long)timeleft.tv_sec * 1000) + (timeleft.tv_nsec / 1000000);
}
static inline
int timespec_to_str(char * const s, const size_t sz, const struct timespec * const ts)
{
	if (timespec_isset(ts))
		return snprintf(s, sz, "%lu.%09ld", ts->tv_sec, ts->tv_nsec);
	else
		return snprintf(s, sz, "(unset)");
}


#define zmq_send_protobuf(s, type, data, flags)  do{  \
	size_t _pbsz = type ## __get_packed_size(data);  \
	uint8_t _pbbuf[_pbsz];  \
	type ## __pack(data, _pbbuf);  \
	zmq_send(s, _pbbuf, _pbsz, flags);  \
}while(0)

#define zmq_recv_protobuf(s, type, data, allocator)  do{  \
	zmq_msg_t _zmqmsg;  \
	assert(!zmq_msg_init(&_zmqmsg));  \
	assert(zmq_msg_recv(&_zmqmsg, s, 0) >= 0);  \
	data = type ## __unpack(allocator, zmq_msg_size(&_zmqmsg), zmq_msg_data(&_zmqmsg));  \
	zmq_msg_close(&_zmqmsg);  \
}while(0)

#endif
