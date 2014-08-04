#include "config.h"

#include <sys/types.h>

static const char _hexchars[0x10] = "0123456789abcdef";

void bin2hex(char *out, const void *in, size_t len)
{
	const unsigned char *p = in;
	while (len--)
	{
		(out++)[0] = _hexchars[p[0] >> 4];
		(out++)[0] = _hexchars[p[0] & 0xf];
		++p;
	}
	out[0] = '\0';
}
