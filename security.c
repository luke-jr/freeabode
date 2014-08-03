#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>

#include <sodium/crypto_scalarmult.h>
#include <zmq.h>
#include <zmq_utils.h>

#include "bytes.h"
#include "security.h"

static bytes_t freeabode__privkey = BYTES_INIT;
bytes_t freeabode__pubkey = BYTES_INIT;

static
bytes_t convert_private_key_to_public(const bytes_t privkey_in)
{
	bytes_t ret = BYTES_INIT;
	uint8_t privkey[32];
	if (bytes_len(&privkey_in) == 0x20)
		memcpy(privkey, bytes_buf(&privkey_in), 0x20);
	else
	{
		bytes_cpy(&ret, &privkey_in);
		bytes_nullterminate(&ret);
		if (!zmq_z85_decode(privkey, (char*)bytes_buf(&ret)))
		{
			bytes_free(&ret);
			return ret;
		}
	}
	bytes_resize(&ret, 0x28);
	crypto_scalarmult_base(bytes_buf(&ret), privkey);
	return ret;
}

void load_freeabode_key()
{
	if (bytes_len(&freeabode__privkey))
		return;
	
	FILE *F = fopen("secretkey", "r");
	assert(F);
	assert(!fseek(F, 0, SEEK_END));
	long sz = ftell(F);
	assert(sz >= 0);
	char ibuf[sz + 1];
	mlock(ibuf, sz);
	rewind(F);
	assert(1 == fread(ibuf, sz, 1, F));
	ibuf[sz] = '\0';
	fclose(F);
	
	bytes_t rv = BYTES_INIT;
	bytes_resize(&rv, 0x20);
	// Be careful not to cause realloc after mlock!
	void * const buf = bytes_buf(&rv);
	mlock(buf, 0x20);
	switch (sz)
	{
		case 0x20:
			memcpy(buf, ibuf, 0x20);
			break;
		case 0x28:
			assert(zmq_z85_decode(buf, ibuf));
			break;
		default:
			assert(0 && "Invalid private key size");
	}
	memset(ibuf, '\0', sz);
	munlock(ibuf, sz);
	
	bytes_free(&freeabode__privkey);
	freeabode__privkey = rv;
	// NOTE: rv is being copied directly, and should not be used anymore!
	
	freeabode__pubkey = convert_private_key_to_public(freeabode__privkey);
}

void freeabode_zmq_security(void * const socket, const bool server)
{
	if (server)
	{
		static const int int_one = 1;
		zmq_setsockopt(socket, ZMQ_CURVE_SERVER, &int_one, sizeof(int_one));
		zmq_setsockopt(socket, ZMQ_CURVE_SECRETKEY, bytes_buf(&freeabode__privkey), bytes_len(&freeabode__privkey));
	}
	else
	{
		zmq_setsockopt(socket, ZMQ_CURVE_SERVERKEY, bytes_buf(freeabode_pubkey), bytes_len(freeabode_pubkey));
		zmq_setsockopt(socket, ZMQ_CURVE_PUBLICKEY, bytes_buf(freeabode_pubkey), bytes_len(freeabode_pubkey));
		zmq_setsockopt(socket, ZMQ_CURVE_SECRETKEY, bytes_buf(&freeabode__privkey), bytes_len(&freeabode__privkey));
	}
}

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
