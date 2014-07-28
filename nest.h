#ifndef FABD_NEST_H
#define FABD_NEST_H

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
	NBPM_FETCONTROL     = 0x0082,
	NBPM_FETPRESENCEACK = 0x008f,
	NBPM_RESET          = 0x00ff,
};

struct nbp_device {
	int _fd;
	bytes_t _rdbuf;
	uint16_t _fet_presence;
};

extern struct nbp_device *nbp_open(const char *path);
extern bool nbp_send(struct nbp_device *, enum nbp_message_type, void *data, size_t datasz);
extern void nbp_close(struct nbp_device *);

static inline
bool nbp_get_fet_presence(struct nbp_device *nbp, enum nbp_fet fet)
{
	return nbp->_fet_presence & (1 << fet);
}

#endif
