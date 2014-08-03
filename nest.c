#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "crc.h"
#include "nest.h"

#define NBP_READ_BUFFER_SIZE  0x10

struct nbp_device *nbp_open(const char * const path)
{
	int fd = open("/dev/ttyO2", O_RDWR | O_NOCTTY);
	if (fd < 0)
		return NULL;
	
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
			._present = FTS_UNKNOWN,
			._asserted = FTS_UNKNOWN,
			._ts_last_shutoff = ts_now,
		};
	return nbp;
}

bool nbp_send(struct nbp_device *nbp, enum nbp_message_type cmd, void *data, size_t datasz)
{
	int fd = nbp->_fd;
	size_t bufsz = 3 + 2 + 2 + datasz + 2;
	uint8_t buf[bufsz];
	buf[0] = '\xd5';
	buf[1] = '\xaa';
	buf[2] = '\x96';
	buf[3] = cmd & 0xff;
	buf[4] = cmd >> 8;
	buf[5] = datasz & 0xff;
	buf[6] = datasz >> 8;
	memcpy(&buf[7], data, datasz);
	uint16_t crc = crc16ccitt(&buf[3], 2 + 2 + datasz);
	buf[7 + datasz] = crc & 0xff;
	buf[8 + datasz] = crc >> 8;
	return (bufsz == write(fd, buf, bufsz));
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
		case NBPM_WEATHER:
			if (sz >= 4)
			{
				uint16_t temperature = buf[0] | (((uint16_t)buf[1]) << 8);
				uint16_t humidity = buf[2] | (((uint16_t)buf[3]) << 8);
				
				nbp->temperature = temperature;
				nbp->humidity = humidity;
				nbp->last_weather_update = *now;
				
				if (nbp->cb_msg_weather)
					nbp->cb_msg_weather(nbp, now, temperature, humidity);
			}
			break;
		case NBPM_FET_PRESENCE:
		{
			nbp_send(nbp, NBPM_FET_PRESENCE_ACK, buf, sz);
			
			uint16_t p = 0;
			for (int i = 0; i < sz; ++i)
			{
				if (i < NBPF__COUNT)
					nbp->_fet[i]._present = buf[i];
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
	
	while (bytes_len(rdbuf) >= 3 + 2 + 2 + 2)
	{
		uint8_t * const buf = bytes_buf(rdbuf);
		if (buf[0] != '\xd5' || buf[1] != '\xaa' || buf[2] != '\x96')
		{
invalid: ;
			int pos = bytes_find_next(rdbuf, '\xd5', 1);
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
		if (bytes_len(rdbuf) < 3 + 2 + 2 + datasz + 2)
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

bool nbp_control_fet(struct nbp_device * const nbp, const enum nbp_fet fet, const bool connect)
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
	return true;
}
