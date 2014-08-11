#ifndef FABD_LOGGING_H
#define FABD_LOGGING_H

#include <syslog.h>

extern void applog(int loglevel, const char *fmt, ...);

#endif
