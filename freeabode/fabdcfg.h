#ifndef FABD_FABDCFG_H
#define FABD_FABDCFG_H

#include <stdbool.h>

#include <jansson.h>

extern void fabdcfg_load_directory(void);
extern void fabdcfg_load_device(const char *devid);
extern const char *fabd_common_argv(int argc, char **argv, const char *type);

extern json_t *fabdcfg_device_get(const char *devid, const char *key);
extern const char *fabdcfg_device_getstr(const char *devid, const char *key);
extern bool fabdcfg_device_checktype(const char *devid, const char *type);

extern bool fabdcfg_zmq_bind(const char *devid, const char *servername, void *socket);
extern bool fabdcfg_zmq_connect(const char *devid, const char *clientname, void *socket);

#endif
