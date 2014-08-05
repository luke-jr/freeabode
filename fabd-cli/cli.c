#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <zmq.h>
#include <jansson.h>

#include <freeabode/freeabode.pb-c.h>
#include <freeabode/json.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		printf("Usage: %s <uri> '<json>'\n", argv[0]);
		exit(1);
	}
	
	load_freeabode_key();
	
	void *my_zmq_context, *ctl;
	my_zmq_context = zmq_ctx_new();
	ctl = zmq_socket(my_zmq_context, ZMQ_REQ);
	
	freeabode_zmq_security(ctl, false);
	
	assert(!zmq_connect(ctl, argv[1]));
	
	{
		json_error_t jserr;
		json_t *json_req = json_loads(argv[2], 0, &jserr);
		if (!json_req)
		{
			printf("JSON parse error at %lu bytes: %s\n", (unsigned long)jserr.position, jserr.text);
			exit(1);
		}
		int errcount = 0;
		PbRequest *pb_req = json_to_protobuf(&pb_request__descriptor, json_req, &errcount);
		json_decref(json_req);
		if (errcount)
		{
			printf("%d errors converting JSON to PbRequest\n", errcount);
			exit(1);
		}
		zmq_send_protobuf(ctl, pb_request, pb_req, 0);
		pb_request__free_unpacked(pb_req, NULL);
	}
	
	{
		PbRequestReply *reply;
		zmq_recv_protobuf(ctl, pb_request_reply, reply, NULL);
		int errcount = 0;
		json_t *json_reply = protobuf_to_json(reply, &errcount);
		if (errcount)
			printf("WARNING: %d errors converting PbRequestReply to JSON\n", errcount);
		pb_request_reply__free_unpacked(reply, NULL);
		json_dumpf(json_reply, stdout, JSON_INDENT(4) | JSON_ENSURE_ASCII | JSON_SORT_KEYS);
		puts("");
		json_decref(json_reply);
	}
	
	zmq_close(ctl);
	zmq_ctx_destroy(my_zmq_context);
	
	return 0;
}
