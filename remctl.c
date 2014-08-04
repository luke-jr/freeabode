#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <time.h>

#include <zmq.h>

#include "freeabode.pb-c.h"
#include "security.h"
#include "util.h"

int main(int argc, char **argv)
{
	if (argc < 3 || argc > 4)
	{
		printf("Usage: %s <wire> <1/0> [uri]\n", argv[0]);
		exit(1);
	}
	const ProtobufCEnumValue *enumdes = protobuf_c_enum_descriptor_get_value_by_name(&pb_hvacwires__descriptor, argv[1]);
	if (!enumdes)
	{
		printf("Unknown wire \"%s\"\n", argv[1]);
		exit(1);
	}
	
	if (argv[2][1] != '\0' || (argv[2][0] & 0xfe) != 0x30)
	{
		printf("Unknown value: \"%s\"\n", argv[2]);
		exit(1);
	}
	
	load_freeabode_key();
	
	void *my_zmq_context, *ctl;
	my_zmq_context = zmq_ctx_new();
	ctl = zmq_socket(my_zmq_context, ZMQ_REQ);
	
	freeabode_zmq_security(ctl, false);
	
	assert(!zmq_connect(ctl, argv[3] ?: "ipc://nbp.ipc"));
	
	PbRequest req = PB_REQUEST__INIT;
	req.n_sethvacwire = 1;
	req.sethvacwire = malloc(sizeof(*req.sethvacwire));
	req.sethvacwire[0] = malloc(sizeof(*req.sethvacwire[0]));
	pb_set_hvacwire_request__init(req.sethvacwire[0]);
	req.sethvacwire[0]->wire = enumdes->value;
	req.sethvacwire[0]->connect = atoi(argv[2]);
	
	zmq_send_protobuf(ctl, pb_request, &req, 0);
	PbRequestReply *reply;
	zmq_recv_protobuf(ctl, pb_request_reply, reply, NULL);
	
	assert(reply->n_sethvacwiresuccess >= 1);
	bool rv = !reply->sethvacwiresuccess[0];
	if (rv)
		puts("Error changing FET");
	pb_request_reply__free_unpacked(reply, NULL);
	
	zmq_close(ctl);
	zmq_ctx_destroy(my_zmq_context);
	
	return rv;
}
