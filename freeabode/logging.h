#ifndef FABD_LOGGING_H
#define FABD_LOGGING_H

#include <syslog.h>

__attribute__((format(printf, 2, 3)))
extern void applog(int loglevel, const char *fmt, ...);

#endif
