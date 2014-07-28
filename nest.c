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

enum nbp_message_type {
	NBPM_FETCONTROL     = 0x0082,
	NBPM_FETPRESENCEACK = 0x008f,
	NBPM_RESET          = 0x00ff,
};

bool nbp_send(int fd, enum nbp_message_type cmd, void *data, size_t datasz)
{
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

int main(int argc, char **argv)
{
	int fd = open("/dev/ttyO2", O_RDWR | O_NOCTTY);
	if (fd < 0)
	{
		perror("Cannot open /dev/ttyO2");
		return 1;
	}
	nbp_send(fd, NBPM_RESET, NULL, 0);
	return 0;
}