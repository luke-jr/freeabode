#ifndef FABD_NEST_H
#define FABD_NEST_H

#include <time.h>

#include "bytes.h"

enum nbp_fet {
	NBPF_W1   =   0,
	NBPF_Y1   =   1,
	NBPF_G    =   2,
	NBPF_OB   =   3,
	NBPF_W2   =   4,
	
	NBPF_Y2   =   7,
	
	NBPF_Star = 0xb,
};

enum nbp_message_type {
	NBPM_LOG            = 0x0001,
	NBPM_WEATHER        = 0x0002,
	NBPM_FET_PRESENCE   = 0x0004,
	NBPM_FETCONTROL     = 0x0082,
	NBPM_REQ_PERIODIC   = 0x0083,
	NBPM_FET_PRESENCE_ACK = 0x008f,
	NBPM_RESET          = 0x00ff,
};

struct nbp_device {
	void (*cb_msg)(struct nbp_device *, const struct timespec *now, enum nbp_message_type, const void *data, size_t datasz);
	void (*cb_msg_fet_presence)(struct nbp_device *, const struct timespec *now, uint16_t fet_bitmask);
	void (*cb_msg_log)(struct nbp_device *, const struct timespec *now, const char *);
	void (*cb_msg_weather)(struct nbp_device *, const struct timespec *now, uint16_t temperature, uint16_t humidity);
	
	struct timespec last_weather_update;
	uint16_t temperature;  // centi-celcius
	uint16_t humidity;     // per-millis
	
	int _fd;
	bytes_t _rdbuf;
	uint16_t _fet_presence;
};

extern struct nbp_device *nbp_open(const char *path);
extern bool nbp_send(struct nbp_device *, enum nbp_message_type, void *data, size_t datasz);
extern void nbp_read(struct nbp_device *);
extern void nbp_close(struct nbp_device *);

static inline
bool nbp_get_fet_presence(struct nbp_device *nbp, enum nbp_fet fet)
{
	return nbp->_fet_presence & (1 << fet);
}

#endif
