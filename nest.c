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
	
	struct nbp_device *nbp = malloc(sizeof(*nbp));
	*nbp = (struct nbp_device){
		._fd = fd,
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
void _nbp_read_one(struct nbp_device * const nbp, const int c)
{
	switch (nbp->_fdstate)
	{
		case 0:
invalid:
			if (c == 0xd5)
	}
}

void nbp_read(struct nbp_device * const nbp)
{
	int fd = nbp->_fd;
	bytes_t * const rdbuf = &nbp->_rdbuf;
	
	{
		void * const buf = bytes_preappend(rdbuf, NBP_READ_BUFFER_SIZE);
		ssize_t rsz = read(fd, buf, NBP_READ_BUFFER_SIZE);
		if (rsz <= 0)
			return;
		bytes_postappend(rdbuf, rsz);
	}
	
	if (bytes_len(rdbuf) < 3 + 2 + 2 + 2)
		return;
	
	{
		uint8_t * const buf = bytes_buf(rdbuf);
		if (buf[0] != '\xd5' || buf[1] != '\xaa' || buf[2] != '\x96')
		{
invalid:
			
		}
		
	}
}

void nbp_close(struct nbp_device * const nbp)
{
	close(nbp->_fd);
	free(nbp);
}
