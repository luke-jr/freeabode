#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <zmq.h>
#include <zmq_utils.h>

#include "bytes.h"
#include "main.h"

static
void zap_handler(void *handler)
{
	static const char my_zap_ver[3] = "1.0";
	char buf[0x100], reqid[0x100];
	int sz, reqidsz;
	bool problem;
	while (true)
	{
		problem = false;
		
		// version
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		if (sz < 0)
			break;
		buf[sz] = '\0';
		if (strcmp(buf, my_zap_ver))
			// Version mismatch
			problem = true;
		
		// request id
		reqidsz = zmq_recv(handler, reqid, sizeof(reqid) - 1, 0);
		// domain
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		// address
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		// identity
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		// mechanism
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		buf[sz] = '\0';
		if (strcmp("CURVE", buf))
			// bad mechanism
			problem = true;
		// credentials
		sz = zmq_recv(handler, buf, sizeof(buf) - 1, 0);
		if (sz < 0x20)
			// key too short
			problem = true;
		else
		if (!problem)
		{
			if (memcmp(bytes_buf(freeabode_pubkey), buf, 0x20))
				problem = true;
		}
		
		zmq_send(handler, my_zap_ver, sizeof(my_zap_ver), ZMQ_SNDMORE);
		zmq_send(handler, reqid, reqidsz, ZMQ_SNDMORE);
		zmq_send(handler, problem ? "400" : "200", 3, ZMQ_SNDMORE);
		zmq_send(handler, NULL, 0, ZMQ_SNDMORE);
		zmq_send(handler, NULL, 0, ZMQ_SNDMORE);
		zmq_send(handler, NULL, 0, 0);
	}
	zmq_close (handler);
}

void start_zap_handler(void *ctx)
{
	void *handler = zmq_socket(ctx, ZMQ_REP);
	assert(handler);
	assert(!zmq_bind(handler, "inproc://zeromq.zap.01"));
	zmq_threadstart(&zap_handler, handler);
}
