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
