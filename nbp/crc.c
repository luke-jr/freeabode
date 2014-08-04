#include "config.h"

#include <stddef.h>
#include <stdint.h>

static uint16_t crctab[0x100];

__attribute__((constructor))
static
void tabinit() {
	const int width = 0x10;
	const uint16_t poly = 0x1021;
	uint32_t r, x;
	
	for (int i = 0; i < 256; ++i)
	{
		r = i << (width - 8);
		for (int j = 0; j < 8; ++j)
		{
			if (r & (1 << (width - 1)))
				r = (r << 1) ^ poly;
			else
				r <<= 1;
		}
		x = r & ((1 << width) - 1);
		crctab[i] = x;
	}
}

uint16_t crc16ccitt(const void * const bufp, size_t sz)
{
	const unsigned char * const buf = bufp;
	const int width = 0x10;
	const int init = 0;
	const int xorout = 0;
	uint16_t *tab = crctab;
	
	uint32_t crc = init;
	uint32_t mask = (1ULL << width) - 1;
	for (int pos = 0; pos < sz; ++pos)
		crc = ((crc << 8)) ^ tab[((crc >> (width - 8)) ^ buf[pos]) & 0xff];
	
	crc ^= xorout;
	return crc & mask;
}
