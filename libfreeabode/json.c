#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <protobuf-c/protobuf-c.h>
#include <jansson.h>

#include "util.h"
#include "json.h"

json_t *fabd_json_array(json_t * const j)
{
	if (json_is_array(j))
		return json_incref(j);
	
	json_t *jj = json_array();
	if (!jj)
		return NULL;
	if (json_array_append(jj, j))
	{
		json_decref(jj);
		return NULL;
	}
	return jj;
}

static
bool pb_is_optional_and_missing(const ProtobufCFieldDescriptor * const pbfield, void *_pb)
{
	if (pbfield->label != PROTOBUF_C_LABEL_OPTIONAL)
		return false;
	
	if (pbfield->quantifier_offset)
	{
		protobuf_c_boolean *bp = _pb + pbfield->quantifier_offset;
		return !*bp;
	}
	else
	{
		// Message or string
		void **p = _pb + pbfield->offset;
		return !*p;
	}
}

static
void j2p_assign_field(void * const out_pb, const ProtobufCFieldDescriptor * const pbfield, json_t * const jval, int * const errcount)
{
	if (json_is_null(jval))
		// Ignore null entirely
		return;
	if (json_is_array(jval))
		// Arrays only allowed one level deep
		goto err;
	if ((bool)json_is_object(jval) != (bool)(pbfield->type == PROTOBUF_C_TYPE_MESSAGE))
		// Objects can only be encoded as submessages and vice-versa
		goto err;
	if (pbfield->type == PROTOBUF_C_TYPE_BYTES && json_is_boolean(jval))
		// true/false make no sense into bytes
		goto err;
	void *addr = out_pb + pbfield->offset;
	if (pbfield->label == PROTOBUF_C_LABEL_OPTIONAL)
	{
		// FIXME: This doesn't check already-present required fields - which isn't possible :(
		if (!pb_is_optional_and_missing(pbfield, out_pb))
			// Already present
			goto err;
	}
	else
	if (pbfield->label == PROTOBUF_C_LABEL_REPEATED)
	{
		// addr is actually an array of items
		void ** const p = addr;
		size_t *np = out_pb + pbfield->quantifier_offset;
		size_t item_sz = 0;
		switch (pbfield->type)
		{
			case PROTOBUF_C_TYPE_INT32:
			case PROTOBUF_C_TYPE_SINT32:
			case PROTOBUF_C_TYPE_SFIXED32:
			case PROTOBUF_C_TYPE_UINT32:
			case PROTOBUF_C_TYPE_FIXED32:
				item_sz = 4;
				break;
			case PROTOBUF_C_TYPE_INT64:
			case PROTOBUF_C_TYPE_SINT64:
			case PROTOBUF_C_TYPE_SFIXED64:
			case PROTOBUF_C_TYPE_UINT64:
			case PROTOBUF_C_TYPE_FIXED64:
				item_sz = 8;
				break;
			case PROTOBUF_C_TYPE_FLOAT:
				item_sz = sizeof(float);
				break;
			case PROTOBUF_C_TYPE_DOUBLE:
				item_sz = sizeof(double);
				break;
			case PROTOBUF_C_TYPE_BOOL:
				item_sz = 1;
				break;
			case PROTOBUF_C_TYPE_ENUM:
				item_sz = sizeof(int);
				break;
			case PROTOBUF_C_TYPE_STRING:
			case PROTOBUF_C_TYPE_MESSAGE:
				item_sz = sizeof(void *);
				break;
			case PROTOBUF_C_TYPE_BYTES:
				item_sz = sizeof(ProtobufCBinaryData);
				break;
		}
		if (!item_sz)
			// In theory, we can handle this using a default case in the switch, but we want warnings during compile if a new case is added to the types :)
			goto err;
		if (*p)
		{
			// already allocated
			void *q = realloc(*p, item_sz * (*np + 1));
			if (!q)
				goto err;
			*p = q;
		}
		else
		{
			// first item
			*p = malloc(item_sz);
			if (!*p)
				goto err;
		}
		addr = *p + (item_sz * *np);
	}
	switch (pbfield->type)
	{
		case PROTOBUF_C_TYPE_INT32:
		case PROTOBUF_C_TYPE_SINT32:
		case PROTOBUF_C_TYPE_SFIXED32:
		case PROTOBUF_C_TYPE_UINT32:
		case PROTOBUF_C_TYPE_FIXED32:
		case PROTOBUF_C_TYPE_INT64:
		case PROTOBUF_C_TYPE_SINT64:
		case PROTOBUF_C_TYPE_SFIXED64:
		case PROTOBUF_C_TYPE_UINT64:
		case PROTOBUF_C_TYPE_FIXED64:
		{
			bool neg = false;
			unsigned long long n;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					char *endptr;
					neg = (s[0] == '-');
					if (neg)
						++s;
					if (!s[0])
						goto err;
					errno = 0;
					n = strtoull(s, &endptr, 0);
					if (endptr[0] || errno)
						goto err;
					break;
				}
				case JSON_INTEGER:
				{
					json_int_t pn = json_integer_value(jval);
					if (pn < 0)
						neg = true;
					n = ullabs(pn);
					break;
				}
				case JSON_REAL:
				{
					double pn = json_real_value(jval);
					if (pn < 0)
						neg = true;
					n = fabs(pn);
					break;
				}
				case JSON_TRUE:
					n = 1;
					break;
				case JSON_FALSE:
					n = 0;
					break;
				default:
					goto err;
			}
			
			switch (pbfield->type)
			{
#define ASSIGN_FIELD(TYPE, TYPE_MIN, TYPE_MAX)  do{  \
					TYPE *p = addr;  \
					if (neg)  \
					{  \
						if (n > TYPE_MIN)  \
							goto err;  \
						*p = -(long long)n;  \
					}  \
					else  \
					{  \
						if (n > TYPE_MAX)  \
							goto err;  \
						*p = n;  \
					}  \
				}while(0)
				case PROTOBUF_C_TYPE_INT32:
				case PROTOBUF_C_TYPE_SINT32:
				case PROTOBUF_C_TYPE_SFIXED32:
					ASSIGN_FIELD(int32_t, ullabs(INT32_MIN), INT32_MAX);
					break;
				case PROTOBUF_C_TYPE_UINT32:
				case PROTOBUF_C_TYPE_FIXED32:
					ASSIGN_FIELD(uint32_t, 0, UINT32_MAX);
					break;
				case PROTOBUF_C_TYPE_INT64:
				case PROTOBUF_C_TYPE_SINT64:
				case PROTOBUF_C_TYPE_SFIXED64:
					ASSIGN_FIELD(int64_t, ullabs(INT64_MIN), INT64_MAX);
					break;
				case PROTOBUF_C_TYPE_UINT64:
				case PROTOBUF_C_TYPE_FIXED64:
					ASSIGN_FIELD(uint64_t, 0, UINT64_MAX);
					break;
				default:
					goto err;
			}
			break;
		}
		
		case PROTOBUF_C_TYPE_FLOAT:
		case PROTOBUF_C_TYPE_DOUBLE:
		{
			double n;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					char *endptr;
					if (!s[0])
						goto err;
					errno = 0;
					n = strtod(s, &endptr);
					if (endptr[0] || errno)
						goto err;
					break;
				}
				case JSON_INTEGER:
					n = json_integer_value(jval);
					break;
				case JSON_REAL:
					n = json_real_value(jval);
					break;
				case JSON_TRUE:
					n = 1;
					break;
				case JSON_FALSE:
					n = 0;
					break;
				default:
					goto err;
			}
			
			if (pbfield->type == PROTOBUF_C_TYPE_FLOAT)
			{
				if (n > FLT_MAX || n < FLT_MIN)
					goto err;
				float *p = addr;
				*p = n;
			}
			else  // PROTOBUF_C_TYPE_DOUBLE
			{
				double *p = addr;
				*p = n;
			}
			break;
		}
		
		case PROTOBUF_C_TYPE_BOOL:
		{
			protobuf_c_boolean *p = addr;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					char *endptr;
					bool b = fabd_strtobool(s, &endptr);
					if (endptr[0])
						goto err;
					*p = b;
					break;
				}
				case JSON_INTEGER:
					*p = json_integer_value(jval);
					break;
				case JSON_REAL:
					*p = json_real_value(jval);
					break;
				case JSON_TRUE:
					*p = true;
					break;
				case JSON_FALSE:
					*p = false;
					break;
				default:
					goto err;
			}
			break;
		}
		
		case PROTOBUF_C_TYPE_ENUM:
		{
			int n;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					char *endptr;
					if (!s[0])
						goto err;
					errno = 0;
					long pn = strtol(s, &endptr, 0);
					if (endptr[0] || errno || pn > INT_MAX || pn < INT_MIN)
					{
						// Try lookup by name
						const ProtobufCEnumValue *enumv = protobuf_c_enum_descriptor_get_value_by_name(pbfield->descriptor, s);
						if (!enumv)
							goto err;
						n = enumv->value;
					}
					else
						n = pn;
					break;
				}
				case JSON_INTEGER:
				{
					json_int_t pn = json_integer_value(jval);
					if (pn > INT_MAX || pn < INT_MIN)
						goto err;
					n = pn;
					break;
				}
				case JSON_REAL:
				{
					double d = json_real_value(jval);
					if (d > INT_MAX || d < INT_MIN || d - fabs(d))
						goto err;
					n = d;
					break;
				}
				case JSON_TRUE:
					n = 1;
					break;
				case JSON_FALSE:
					n = 0;
					break;
				default:
					goto err;
			}
			
			int *p = addr;
			*p = n;
			
			break;
		}
		
		case PROTOBUF_C_TYPE_STRING:
		{
			char **p = addr;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					size_t sz = strlen(s) + 1;
					char *out = malloc(sz);
					if (!out)
						goto err;
					memcpy(out, s, sz);
					*p = out;
					break;
				}
				default:
				{
					char *out = json_dumps(jval, JSON_ENCODE_ANY);
					if (!out)
						goto err;
					*p = out;
					break;
				}
			}
			break;
		}
		
		case PROTOBUF_C_TYPE_BYTES:
		{
			ProtobufCBinaryData *p = addr;
			switch (json_typeof(jval))
			{
				case JSON_STRING:
				{
					const char *s = json_string_value(jval);
					size_t sz = strlen(s) / 2;
					uint8_t *out = malloc(sz);
					if (!out)
						goto err;
					if (!hex2bin(out, s, sz))
						goto err;
					p->len = sz;
					p->data = out;
					break;
				}
				// TODO: Maybe allow array of ints?
				default:
					goto err;
			}
			break;
		}
		
		case PROTOBUF_C_TYPE_MESSAGE:
		{
			void **p = addr, *msg;
			const struct ProtobufCMessageDescriptor *des = pbfield->descriptor;
			msg = json_to_protobuf(des, jval, errcount);
			if (!msg)
				// errcount already incremented by json_to_protobuf
				return;
			*p = msg;
			break;
		}
		
		default:
			goto err;
	}
	if (pbfield->label == PROTOBUF_C_LABEL_REPEATED)
	{
		size_t *np = out_pb + pbfield->quantifier_offset;
		++*np;
	}
	else
	if (pbfield->label == PROTOBUF_C_LABEL_OPTIONAL && pbfield->quantifier_offset)
	{
		protobuf_c_boolean *bp = out_pb + pbfield->quantifier_offset;
		*bp = true;
	}
	return;

err:
	++*errcount;
}

void *json_to_protobuf(const ProtobufCMessageDescriptor * const des, json_t * const json, int * const errcount)
{
	ProtobufCMessage *pb = malloc(des->sizeof_message);
	if (!pb)
	{
		++*errcount;
		return NULL;
	}
	des->message_init(pb);
	
	for (void *jiter = json_object_iter(json); jiter; jiter = json_object_iter_next(json, jiter))
	{
		const ProtobufCFieldDescriptor *pbfield = protobuf_c_message_descriptor_get_field_by_name(des, json_object_iter_key(jiter));
		if (!pbfield)
		{
			++*errcount;
			continue;
		}
		json_t *jval = json_object_iter_value(jiter);
		if (json_is_array(jval))
		{
			size_t sz = json_array_size(jval);
			for (size_t i = 0; i < sz; ++i)
				j2p_assign_field(pb, pbfield, json_array_get(jval, i), errcount);
		}
		else
			j2p_assign_field(pb, pbfield, jval, errcount);
	}
	return pb;
}

static
json_t *p2j_elem(void ** const addr_p, const ProtobufCFieldDescriptor * const pbfield, int * const errcount)
{
	void * const addr = *addr_p;
	json_t *j = NULL;
	switch (pbfield->type)
	{
#define P2J_N(JTYPE, TYPE)  do{  \
			*addr_p += sizeof(TYPE);  \
			TYPE *p = addr;  \
			j = json_ ## JTYPE(*p);  \
		}while(0)
		case PROTOBUF_C_TYPE_INT32:
		case PROTOBUF_C_TYPE_SINT32:
		case PROTOBUF_C_TYPE_SFIXED32:
			P2J_N(integer, int32_t);
			break;
		case PROTOBUF_C_TYPE_INT64:
		case PROTOBUF_C_TYPE_SINT64:
		case PROTOBUF_C_TYPE_SFIXED64:
			P2J_N(integer, int64_t);
			break;
		case PROTOBUF_C_TYPE_UINT32:
		case PROTOBUF_C_TYPE_FIXED32:
			P2J_N(integer, uint32_t);
			break;
		case PROTOBUF_C_TYPE_UINT64:
		case PROTOBUF_C_TYPE_FIXED64:
			P2J_N(integer, uint64_t);
			break;
		case PROTOBUF_C_TYPE_FLOAT:
			P2J_N(real, float);
			break;
		case PROTOBUF_C_TYPE_DOUBLE:
			P2J_N(real, double);
			break;
		case PROTOBUF_C_TYPE_BOOL:
		{
			*addr_p += 1;
			protobuf_c_boolean *p = addr;
			j = (*p) ? json_true() : json_false();
			break;
		}
		case PROTOBUF_C_TYPE_ENUM:
		{
			*addr_p += sizeof(int);
			int *p = addr;
			const ProtobufCEnumValue *enumv = protobuf_c_enum_descriptor_get_value(pbfield->descriptor, *p);
			if (enumv)
				j = json_string(enumv->name);
			else
				j = json_integer(*p);
			break;
		}
		case PROTOBUF_C_TYPE_STRING:
		{
			*addr_p += sizeof(char *);
			char **p = addr;
			if (!*p)
				return NULL;
			j = json_string(*p);
			break;
		}
		case PROTOBUF_C_TYPE_BYTES:
		{
			*addr_p += sizeof(ProtobufCBinaryData);
			ProtobufCBinaryData *p = addr;
			char hex[(p->len * 2) + 1];
			bin2hex(hex, p->data, p->len);
			j = json_string(hex);
			break;
		}
		case PROTOBUF_C_TYPE_MESSAGE:
		{
			*addr_p += sizeof(void *);
			void **p = addr;
			if (!*p)
				return NULL;
			j = protobuf_to_json(*p, errcount);
			if (!j)
				// Will be incremented twice, from protobuf_to_json and again at return
				--*errcount;
			break;
		}
	}
	if (!j)
		++*errcount;
	return j;
}

json_t *protobuf_to_json(void * const _pb, int * const errcount)
{
	ProtobufCMessage * const pb = _pb;
	const ProtobufCMessageDescriptor * const des = pb->descriptor;
	json_t *j = json_object();
	if (!j)
	{
		++*errcount;
		return NULL;
	}
	
	for (unsigned i = 0; i < des->n_fields; ++i)
	{
		const ProtobufCFieldDescriptor * const pbfield = &des->fields[i];
		void *addr = _pb + pbfield->offset;
		json_t *jitem;
		if (pbfield->label == PROTOBUF_C_LABEL_REPEATED)
		{
			size_t *np = _pb + pbfield->quantifier_offset;
			void **p = addr, *elem;
			if (!*p)
				continue;
			
			jitem = json_array();
			if (!jitem)
			{
				++*errcount;
				continue;
			}
			elem = *p;
			for (size_t j = 0; j < *np; ++j)
			{
				json_t *jelem = p2j_elem(&elem, pbfield, errcount);
				if (!jelem)
					continue;
				if (json_array_append_new(jitem, jelem) == -1)
				{
					json_decref(jelem);
					continue;
				}
			}
		}
		else
		{
			if (pb_is_optional_and_missing(pbfield, _pb))
				continue;
			
			jitem = p2j_elem(&addr, pbfield, errcount);
			if (!jitem)
				continue;
		}
		
		if (-1 == json_object_set_new(j, pbfield->name, jitem))
		{
			++*errcount;
			json_decref(jitem);
		}
	}
	
	// TODO: unknown fields
	
	return j;
}
