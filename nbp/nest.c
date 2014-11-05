#include "config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

#include "crc.h"
#include "nest.h"

#define NBP_DEFAULT_SHUTOFF_DELAY  { .tv_sec = 337, .tv_nsec = 500000000, }

#define NBP_READ_BUFFER_SIZE  0x10

struct nbp_device *nbp_open(const char * const path)
{
#ifdef NBP_SIMULATE
	int fd = fileno(stdin);
#else
	int fd = open("/dev/ttyO2", O_RDWR | O_NOCTTY);
	if (fd < 0)
		return NULL;
	
	struct termios tios;
	tcgetattr(fd, &tios);
	speed_t speed = B115200;
	cfsetispeed(&tios, speed);
	cfsetospeed(&tios, speed);
	
	tios.c_cflag &= ~(CSIZE | PARENB);
	tios.c_cflag |= CS8 | CREAD | CLOCAL;
	tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tios.c_oflag &= ~OPOST;
	tios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	
	tcsetattr(fd, TCSANOW, &tios);
	
	tcflush(fd, TCIOFLUSH);
#endif
	
	struct timespec ts_now;
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	
	void *mem = malloc(sizeof(struct nbp_device) + (sizeof(struct nbp_fet_data) * NBPF__COUNT));
	struct nbp_device *nbp = mem;
	*nbp = (struct nbp_device){
		._fd = fd,
		._fet = mem + sizeof(struct nbp_device),
	};
	for (int i = 0; i < NBPF__COUNT; ++i)
		nbp->_fet[i] = (struct nbp_fet_data){
			.ts_shutoff_delay = NBP_DEFAULT_SHUTOFF_DELAY,
			._present = FTS_UNKNOWN,
			._asserted = FTS_UNKNOWN,
			._ts_last_shutoff = ts_now,
		};
	return nbp;
}

bool nbp_send(struct nbp_device *nbp, enum nbp_message_type cmd, void *data, size_t datasz)
{
#ifdef NBP_SIMULATE
	char hexdata[(datasz * 2) + 1];
	bin2hex(hexdata, data, datasz);
	printf("%04lx %s\n", (unsigned long)cmd, hexdata);
	return true;
#endif
	
	int fd = nbp->_fd;
	size_t bufsz = 3 + 2 + 2 + datasz + 2;
	uint8_t buf[bufsz];
	buf[0] = 0xd5;
	buf[1] = 0xaa;
	buf[2] = 0x96;
	buf[3] = cmd & 0xff;
	buf[4] = cmd >> 8;
	buf[5] = datasz & 0xff;
	buf[6] = datasz >> 8;
	memcpy(&buf[7], data, datasz);
	uint16_t crc = crc16ccitt(&buf[3], 2 + 2 + datasz);
	buf[7 + datasz] = crc & 0xff;
	buf[8 + datasz] = crc >> 8;
	return ((ssize_t)bufsz == write(fd, buf, bufsz));
}

static
void nbp_got_message(struct nbp_device * const nbp, uint8_t * const buf, const size_t sz, const struct timespec *now)
{
	enum nbp_message_type mtype = buf[-4] | (((uint16_t)buf[-3]) << 8);
	
	if (nbp->cb_msg)
		nbp->cb_msg(nbp, now, mtype, buf, sz);
	
	switch (mtype)
	{
		case NBPM_LOG:
			if (nbp->cb_msg_log)
			{
				buf[sz] = '\0';
				nbp->cb_msg_log(nbp, now, (void*)buf);
			}
			break;
		case NBPM_POWER_STATUS:
		{
			uint8_t state = buf[0];
			uint8_t flags = buf[1];
			uint8_t px0 = buf[2];
			uint16_t u1 = upk_u16le(buf, 3);
			uint8_t u2 = buf[5];
			uint16_t u3 = upk_u16le(buf, 6);
			uint16_t vi_cV = upk_u16le(buf, 8);
			uint16_t vo_mV = upk_u16le(buf, 0xa);
			uint16_t vb_mV = upk_u16le(buf, 0xc);
			uint8_t pins = buf[0xe];
			uint8_t wires = buf[0xf];
			
			nbp->vi_cV = vi_cV;
			nbp->vo_mV = vo_mV;
			nbp->vb_mV = vb_mV;
			nbp->power_flags = flags;
			nbp->last_power_update = *now;
			nbp->has_powerinfo = true;
			
			if (nbp->cb_msg_power_status)
				nbp->cb_msg_power_status(nbp, now, state, flags, px0, u1, u2, u3, vi_cV, vo_mV, vb_mV, pins, wires);
			break;
		}
		case NBPM_WEATHER:
			if (sz >= 4)
			{
				uint16_t temperature = buf[0] | (((uint16_t)buf[1]) << 8);
				uint16_t humidity = buf[2] | (((uint16_t)buf[3]) << 8);
				
				nbp->temperature = temperature;
				nbp->humidity = humidity;
				nbp->last_weather_update = *now;
				nbp->has_weather = true;
				
				if (nbp->cb_msg_weather)
					nbp->cb_msg_weather(nbp, now, temperature, humidity);
			}
			break;
		case NBPM_FET_PRESENCE:
		{
			nbp_send(nbp, NBPM_FET_PRESENCE_ACK, buf, sz);
			
			uint16_t p = 0;
			for (size_t i = 0; i < sz; ++i)
			{
				if (i < NBPF__COUNT)
				{
					if (nbp->_fet[i]._present != FTS_FALSE && nbp->_fet[i]._asserted != FTS_FALSE)
						// For safety, assert disconnection
						nbp_control_fet_unsafe(nbp, i, false);
					nbp->_fet[i]._present = buf[i];
				}
				if (buf[i])
					p |= 1 << i;
			}
			
			if (nbp->cb_msg_fet_presence)
				nbp->cb_msg_fet_presence(nbp, now, p);
			
			break;
		}
		default:
			break;
	}
}

void nbp_read(struct nbp_device * const nbp)
{
	int fd = nbp->_fd;
	bytes_t * const rdbuf = &nbp->_rdbuf;
	struct timespec now;
	
	{
		void * const buf = bytes_preappend(rdbuf, NBP_READ_BUFFER_SIZE);
		ssize_t rsz = read(fd, buf, NBP_READ_BUFFER_SIZE);
		if (rsz <= 0)
			return;
		clock_gettime(CLOCK_MONOTONIC, &now);
		bytes_postappend(rdbuf, rsz);
	}
	
#ifdef NBP_SIMULATE
	int pos;
	while ( (pos = bytes_find(rdbuf, '\n')) != -1)
	{
		char *endptr, *p;
		long msg = strtol((void*)bytes_buf(rdbuf), &endptr, 0x10);
		while (*endptr != '\n' && isspace(*endptr))
			++endptr;
		p = endptr;
		while (*endptr != '\n')
			++endptr;
		size_t datasz = (endptr - p) / 2;
		uint8_t buf[4 + datasz + 1];
		uint8_t *data = &buf[4];
		buf[0] = msg;
		buf[1] = msg >> 8;
		data[datasz] = '\0';
		hex2bin(data, p, datasz);
		bytes_shift(rdbuf, pos + 1);
		nbp_got_message(nbp, data, datasz, &now);
	}
	return;
#endif
	
	while (bytes_len(rdbuf) >= 3 + 2 + 2 + 2)
	{
		uint8_t * const buf = bytes_buf(rdbuf);
		if (buf[0] != 0xd5 || buf[1] != 0xaa || buf[2] != 0x96)
		{
invalid: ;
			int pos = bytes_find_next(rdbuf, 0xd5, 1);
			if (pos == -1)
			{
				bytes_reset(rdbuf);
				break;
			}
			else
			{
				bytes_shift(rdbuf, pos);
				continue;
			}
		}
		uint16_t datasz = buf[5] | (((uint16_t)buf[6]) << 8);
		if (bytes_len(rdbuf) < (unsigned)(3 + 2 + 2 + datasz + 2))
			// Need more data to proceed
			break;
		uint16_t good_crc = crc16ccitt(&buf[3], 2 + 2 + datasz);
		uint16_t actual_crc = buf[7 + datasz] | (((uint16_t)buf[8 + datasz]) << 8);
		if (good_crc != actual_crc)
			goto invalid;
		
		// Entire valid packet found
		nbp_got_message(nbp, &buf[7], datasz, &now);
		
		bytes_shift(rdbuf, 3 + 2 + 2 + datasz + 2);
	}
}

void nbp_close(struct nbp_device * const nbp)
{
	close(nbp->_fd);
	free(nbp);
}

bool nbp_control_fet_unsafe(struct nbp_device * const nbp, const enum nbp_fet fet, const bool connect)
{
	uint8_t data[2] = {fet, connect};
	if (!nbp_send(nbp, NBPM_FET_CONTROL, data, sizeof(data)))
		return false;
	if (fet < NBPF__COUNT)
	{
		if (nbp->_fet[fet]._asserted != FTS_FALSE && !connect)
			clock_gettime(CLOCK_MONOTONIC, &nbp->_fet[fet]._ts_last_shutoff);
		nbp->_fet[fet]._asserted = connect;
	}
	nbp->cb_asserting_fet_control(nbp, fet, connect);
	return true;
}

bool nbp_control_fet(struct nbp_device * const nbp, const enum nbp_fet fet, const bool connect)
{
	if (fet >= NBPF__COUNT)
		// Assume unknown FETs are always unsafe, since we have no safety controls
		return false;
	const struct timespec *ts_shutoff_delay = &nbp->_fet[fet].ts_shutoff_delay;
	if (connect && (ts_shutoff_delay->tv_sec || ts_shutoff_delay->tv_nsec))
	{
		struct timespec ts_soonest_cycle, ts_now;
		timespec_add(&nbp->_fet[fet]._ts_last_shutoff, ts_shutoff_delay, &ts_soonest_cycle);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (timespec_cmp(&ts_now, &ts_soonest_cycle) < 0)
			return false;
	}
	return nbp_control_fet_unsafe(nbp, fet, connect);
}
