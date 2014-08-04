#include "config.h"

#include <stdbool.h>
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

static inline
int _hex2bin_char(const char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return (c - 'a') + 10;
	if (c >= 'A' && c <= 'F')
		return (c - 'A') + 10;
	return -1;
}

bool hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
	int n, o;
	
	while (len--)
	{
		n = _hex2bin_char((hexstr++)[0]);
		if (n == -1)
		{
badchar:
			return false;
		}
		o = _hex2bin_char((hexstr++)[0]);
		if (o == -1)
			goto badchar;
		(p++)[0] = (n << 4) | o;
	}
	
	return !hexstr[0];
}
