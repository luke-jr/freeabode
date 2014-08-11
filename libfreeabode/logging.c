#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

void applog(const int loglevel, const char * const fmt, ...)
{
	struct timeval tv;
	struct tm tm;
	va_list ap, ap2;
	
	gettimeofday(&tv, NULL);
	gmtime_r(&tv.tv_sec, &tm);
	
	va_start(ap, fmt);
	va_copy(ap2, ap);
	int sz = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	
	char buf[0x20 + sz + 1];
	int len = snprintf(buf, sizeof(buf), "[%d-%02d-%02d %02d:%02d:%02d.%06ld] ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (long)tv.tv_usec);
	len += vsnprintf(&buf[len], sizeof(buf)-len, fmt, ap);
	va_end(ap);
	
	strcpy(&buf[len], "\n");
	++len;
	
	fwrite(buf, len, 1, stderr);
	fflush(stderr);
}
