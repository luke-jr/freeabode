#ifndef FABD_JSON_H
#define FABD_JSON_H

#include <protobuf-c/protobuf-c.h>
#include <jansson.h>

extern json_t *fabd_json_array(json_t *);
extern int fabd_json_as_int(const json_t *, int def);

extern void *json_to_protobuf(const ProtobufCMessageDescriptor *, json_t *, int *errcount);
extern json_t *protobuf_to_json(void *pb, int *errcount);

#endif
