#ifndef FABD_NEST_H
#define FABD_NEST_H

#include <time.h>

#include <freeabode/bytes.h>
#include <freeabode/util.h>

enum nbp_fet {
	NBPF_W1   =   0,
	NBPF_Y1   =   1,
	NBPF_G    =   2,
	NBPF_OB   =   3,
	NBPF_W2   =   4,
	
	NBPF_Y2   =   7,
	NBPF_C    =   8,
	NBPF_RC   =   9,
	
	NBPF_Star = 0xb,
	
	NBPF__COUNT = 0xd,
};

enum nbp_message_type {
	NBPM_LOG            = 0x0001,
	NBPM_WEATHER        = 0x0002,
	NBPM_FET_PRESENCE   = 0x0004,
	NBPM_POWER_STATUS   = 0x000b,
	
	NBPM_FET_CONTROL    = 0x0082,
	NBPM_REQ_PERIODIC   = 0x0083,
	NBPM_FET_PRESENCE_ACK = 0x008f,
	NBPM_RESET          = 0x00ff,
};

enum nbp_power_flags {
	NBPPF_NOCHARGE = 0x40,
};

struct nbp_fet_data {
	struct timespec ts_shutoff_delay;
	
	enum fabd_tristate _present;
	enum fabd_tristate _asserted;
	struct timespec _ts_last_shutoff;
};

struct nbp_device {
	void (*cb_msg)(struct nbp_device *, const struct timespec *now, enum nbp_message_type, const void *data, size_t datasz);
	void (*cb_msg_fet_presence)(struct nbp_device *, const struct timespec *now, uint16_t fet_bitmask);
	void (*cb_msg_log)(struct nbp_device *, const struct timespec *now, const char *);
	void (*cb_msg_power_status)(struct nbp_device *, const struct timespec *now, uint8_t state, uint8_t flags, uint8_t px0, uint16_t unknown1, uint8_t unknown2, uint16_t unknown3, uint16_t vi_cV, uint16_t vo_mV, uint16_t vb_mV, uint8_t pins, uint8_t wires);
	void (*cb_msg_weather)(struct nbp_device *, const struct timespec *now, uint16_t temperature, uint16_t humidity);
	void (*cb_asserting_fet_control)(struct nbp_device *, enum nbp_fet, bool connection);
	
	bool has_weather;
	struct timespec last_weather_update;
	uint16_t temperature;  // centi-celcius
	uint16_t humidity;     // per-millis
	
	struct timespec last_power_update;
	uint16_t vi_cV;
	uint16_t vo_mV;
	uint16_t vb_mV;
	
	int _fd;
	bytes_t _rdbuf;
	struct nbp_fet_data *_fet;
};

extern struct nbp_device *nbp_open(const char *path);
extern bool nbp_send(struct nbp_device *, enum nbp_message_type, void *data, size_t datasz);
extern void nbp_read(struct nbp_device *);
extern void nbp_close(struct nbp_device *);

static inline
enum fabd_tristate nbp_get_fet_presence(struct nbp_device *nbp, enum nbp_fet fet)
{
	return (fet >= NBPF__COUNT) ? FTS_UNKNOWN : nbp->_fet[fet]._present;
}

static inline
enum fabd_tristate nbp_get_fet_asserted(struct nbp_device *nbp, enum nbp_fet fet)
{
	return (fet >= NBPF__COUNT) ? FTS_UNKNOWN : nbp->_fet[fet]._asserted;
}

extern bool nbp_control_fet_unsafe(struct nbp_device *, enum nbp_fet, bool connect);
extern bool nbp_control_fet(struct nbp_device *, enum nbp_fet, bool connect);

#endif
