#ifndef FABD_SECURITY_H
#define FABD_SECURITY_H

#include "bytes.h"

extern bytes_t freeabode__pubkey;
#define freeabode_pubkey  ((const bytes_t *)&freeabode__pubkey)
extern void load_freeabode_key(void);

extern void freeabode_zmq_security(void *socket, bool server);

extern void start_zap_handler(void *ctx);

#endif
