#ifndef FABD_UTIL_H
#define FABD_UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#define ullabs(n)  (((n) < 0) ? (ULLONG_MAX - (unsigned long long)(n) + 1) : (unsigned long long)(n))

enum fabd_tristate {
	FTS_FALSE = (int)false,
	FTS_TRUE  = (int)true,
	FTS_UNKNOWN,
};

static const int int_one = 1;

#define fabd_min(a, b)  \
	({  \
		__typeof__(a) _a = (a);  \
		__typeof__(b) _b = (b);  \
		_a < _b ? _a : _b;  \
	})  \
/* End of fabd_min */

#define fabd_max(a, b)  \
	({  \
		__typeof__(a) _a = (a);  \
		__typeof__(b) _b = (b);  \
		_a > _b ? _a : _b;  \
	})  \
/* End of fabd_max */

static inline
uint8_t upk_u8(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return buf[offset];
}

#define upk_u8be(buf, offset)  upk_u8(buf, offset)

static inline
uint16_t upk_u16be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint16_t)buf[offset+0]) <<    8)
	     | (((uint16_t)buf[offset+1]) <<    0);
}

static inline
uint32_t upk_u32be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint32_t)buf[offset+0]) << 0x18)
	     | (((uint32_t)buf[offset+1]) << 0x10)
	     | (((uint32_t)buf[offset+2]) <<    8)
	     | (((uint32_t)buf[offset+3]) <<    0);
}

static inline
uint64_t upk_u64be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint64_t)buf[offset+0]) << 0x38)
	     | (((uint64_t)buf[offset+1]) << 0x30)
	     | (((uint64_t)buf[offset+2]) << 0x28)
	     | (((uint64_t)buf[offset+3]) << 0x20)
	     | (((uint64_t)buf[offset+4]) << 0x18)
	     | (((uint64_t)buf[offset+5]) << 0x10)
	     | (((uint64_t)buf[offset+6]) <<    8)
	     | (((uint64_t)buf[offset+7]) <<    0);
}

#define upk_u8le(buf, offset)  upk_u8(buf, offset)

static inline
uint16_t upk_u16le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint16_t)buf[offset+0]) <<    0)
	     | (((uint16_t)buf[offset+1]) <<    8);
}

static inline
uint32_t upk_u32le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint32_t)buf[offset+0]) <<    0)
	     | (((uint32_t)buf[offset+1]) <<    8)
	     | (((uint32_t)buf[offset+2]) << 0x10)
	     | (((uint32_t)buf[offset+3]) << 0x18);
}

static inline
uint64_t upk_u64le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint64_t)buf[offset+0]) <<    0)
	     | (((uint64_t)buf[offset+1]) <<    8)
	     | (((uint64_t)buf[offset+2]) << 0x10)
	     | (((uint64_t)buf[offset+3]) << 0x18)
	     | (((uint64_t)buf[offset+4]) << 0x20)
	     | (((uint64_t)buf[offset+5]) << 0x28)
	     | (((uint64_t)buf[offset+6]) << 0x30)
	     | (((uint64_t)buf[offset+7]) << 0x38);
}


static inline
void pk_u8(void * const bufp, const int offset, const uint8_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset] = nv;
}

#define pk_u8be(buf, offset, nv)  pk_u8(buf, offset, nv)

static inline
void pk_u16be(void * const bufp, const int offset, const uint16_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    8) & 0xff;
	buf[offset+1] = (nv >>    0) & 0xff;
}

static inline
void pk_u32be(void * const bufp, const int offset, const uint32_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >> 0x18) & 0xff;
	buf[offset+1] = (nv >> 0x10) & 0xff;
	buf[offset+2] = (nv >>    8) & 0xff;
	buf[offset+3] = (nv >>    0) & 0xff;
}

static inline
void pk_u64be(void * const bufp, const int offset, const uint64_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >> 0x38) & 0xff;
	buf[offset+1] = (nv >> 0x30) & 0xff;
	buf[offset+2] = (nv >> 0x28) & 0xff;
	buf[offset+3] = (nv >> 0x20) & 0xff;
	buf[offset+4] = (nv >> 0x18) & 0xff;
	buf[offset+5] = (nv >> 0x10) & 0xff;
	buf[offset+6] = (nv >>    8) & 0xff;
	buf[offset+7] = (nv >>    0) & 0xff;
}

#define pk_u8le(buf, offset, nv)  pk_u8(buf, offset, nv)

static inline
void pk_u16le(void * const bufp, const int offset, const uint16_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
}

static inline
void pk_u32le(void * const bufp, const int offset, const uint32_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
	buf[offset+2] = (nv >> 0x10) & 0xff;
	buf[offset+3] = (nv >> 0x18) & 0xff;
}

static inline
void pk_u64le(void * const bufp, const int offset, const uint64_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
	buf[offset+2] = (nv >> 0x10) & 0xff;
	buf[offset+3] = (nv >> 0x18) & 0xff;
	buf[offset+4] = (nv >> 0x20) & 0xff;
	buf[offset+5] = (nv >> 0x28) & 0xff;
	buf[offset+6] = (nv >> 0x30) & 0xff;
	buf[offset+7] = (nv >> 0x38) & 0xff;
}

extern char *fabd_memndup(const void *, size_t);

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
void timespec_add_ns(const struct timespec * const a, const long long b_ns, struct timespec * const result)
{
	result->tv_sec = a->tv_sec;
	long long tot_nsecs = b_ns + a->tv_nsec;
	if (tot_nsecs >= 1000000000LL)
	{
		result->tv_sec += tot_nsecs / 1000000000LL;
		result->tv_nsec = tot_nsecs % 1000000000LL;
	}
	else
		result->tv_nsec = tot_nsecs;
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
