#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <zmq.h>

#include "fabdcfg.h"
#include "json.h"
#include "util.h"

static const char * const fabd_cfg_dir = "fabd_cfg";
static const char * const fabd_cfg_suffix = ".json";

static json_t *my_directory, *my_configs;

static
char *my_cfg_filepath(const char * const name)
{
	const size_t fabd_cfg_dir_len = strlen(fabd_cfg_dir);
	const size_t namelen = strlen(name);
	const size_t fabd_cfg_suffix_len = strlen(fabd_cfg_suffix);
	const size_t allocsz = fabd_cfg_dir_len + 1 + namelen + fabd_cfg_suffix_len + 1;
	char * const rv = malloc(allocsz);
	assert(rv);
	snprintf(rv, allocsz, "%s/%s%s", fabd_cfg_dir, name, fabd_cfg_suffix);
	return rv;
}

static
json_t *fabdcfg_load_(const char * const name)
{
	char *path = my_cfg_filepath(name);
	json_error_t je;
	json_t * const rv = json_load_file(path, 0, &je);
	free(path);
	assert(rv);
	return rv;
}

void fabdcfg_load_directory()
{
	my_directory = fabdcfg_load_("directory");
	my_configs = json_object();
}

void fabdcfg_load_device(const char * const devid)
{
	json_t *j = fabdcfg_load_(devid);
	if (j)
		json_object_set_new(my_configs, devid, j);
}

const char *fabd_common_argv(int argc, char **argv, const char * const type)
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <device-id>\n", argv[0]);
		exit(1);
	}
	const char * const my_devid = argv[1];
	fabdcfg_load_directory();
	fabdcfg_load_device(my_devid);
	return my_devid;
}

json_t *fabdcfg_device_get(const char * const devid, const char * const key)
{
	json_t *j;
	
	if ( (j = json_object_get(my_configs, devid)) )
		if ( (j = json_object_get(j, key)) )
			return j;
	
	if ( (j = json_object_get(my_directory, "devices")) )
		if ( (j = json_object_get(j, devid)) )
			if ( (j = json_object_get(j, key)) )
				return j;
	
	if ( (j = json_object_get(my_directory, "defaults")) )
		if ( (j = json_object_get(j, key)) )
			return j;
	
	return NULL;
}

bool fabdcfg_device_getbool(const char * const devid, const char * const key, const bool def)
{
	json_t * const j = fabdcfg_device_get(devid, key);
	if (!j) return def;
	switch (json_typeof(j)) {
		case JSON_ARRAY:
			return json_array_size(j) != 0;
		case JSON_STRING:
			return json_string_value(j)[0] != '\0';
		case JSON_INTEGER:
			return json_integer_value(j) != 0;
		case JSON_REAL:
			return json_real_value(j) != 0;
		case JSON_FALSE:
			return false;
		case JSON_NULL:
			return def;
		default:
			return true;
	}
}

const char *fabdcfg_device_getstr(const char * const devid, const char * const key)
{
	json_t * const j = fabdcfg_device_get(devid, key);
	if (!(j && json_is_string(j)))
		return NULL;
	return json_string_value(j);
}

int fabdcfg_device_getint(const char * const devid, const char * const key, const int def)
{
	json_t * const j = fabdcfg_device_get(devid, key);
	return fabd_json_as_int(j, def);
}

bool fabdcfg_device_checktype(const char * const devid, const char * const type)
{
	const char * const atype = fabdcfg_device_getstr(devid, "type");
	return atype && !strcmp(atype, type);
}

static
json_t *fabdcfg_server_get(const char * const devid, const char * const servername)
{
	json_t *j = fabdcfg_device_get(devid, "servers");
	if (!j)
		return NULL;
	return json_object_get(j, servername);
}

bool fabdcfg_zmq_bind(const char * const devid, const char * const servername, void * const socket)
{
	json_t *j = fabdcfg_server_get(devid, servername);
	if (!j)
		return false;
	if (json_is_object(j))
	{
		j = json_object_get(j, "bind");
		if (!j)
			return false;
	}
	j = fabd_json_array(j);
	bool success = true;
	for (size_t i = 0, il = json_array_size(j); i < il; ++i)
	{
		json_t * const jj = json_array_get(j, i);
		if (zmq_bind(socket, json_string_value(jj)))
			success = false;
	}
	json_decref(j);
	return success;
}

static
bool fabd_parse_devuri(const char * const s, char ** const devid, char ** const servername)
{
	if ((!s) || strncmp(s, "fabd:", 5))
		return false;
	const char *p, *q;
	// Simply ignore slashes here for now
	for (p = &s[5]; p[0] == '/'; ++p)
	{}
	// Skip to the end
	for (q = p; q[0] && q[0] != '/'; ++q)
	{}
	*devid = fabd_memndup(p, q-p);
	if (q[0])
		++q;
	*servername = strdup(q);
	return true;
}

static
char *fabdcfg_server_get_connect(const char * const devid, const char * const servername, const char * const from_devid)
{
	bool is_local;
	const char *node;
	{
		json_t * const jnode = fabdcfg_device_get(devid, "node");
		if (json_is_string(jnode))
		{
			node = json_string_value(jnode);
			json_t * const jfromnode = fabdcfg_device_get(from_devid, "node");
			is_local = (json_is_string(jfromnode) && !strcmp(node, json_string_value(jfromnode)));
		}
		else
		{
			is_local = false;
			node = NULL;
		}
	}
	
	json_t *jserver = fabdcfg_server_get(devid, servername), *j;
	if (!jserver)
		return NULL;
	if (json_is_object(jserver))
	{
		j = json_object_get(jserver, "connect");
		if (j)
		{
			// Choose first applicable URI
			j = fabd_json_array(j);
			for (size_t i = 0, il = json_array_size(j); i < il; ++i)
			{
				json_t * const ji = json_array_get(j, i);
				const char * const s = json_string_value(ji);
				
				if (!s)
					continue;
				if ((!strncmp(s, "ipc:", 4)) && !is_local)
					continue;
				
				return strdup(s);
			}
			return NULL;
		}
		j = json_object_get(jserver, "bind");
		if (!j)
			return NULL;
	}
	else
		j = jserver;
	
	// Construct connect from bind
	j = fabd_json_array(j);
	for (size_t i = 0, il = json_array_size(j); i < il; ++i)
	{
		json_t * const ji = json_array_get(j, i);
		const char * const s = json_string_value(ji);
		if (!s)
			continue;
		const char *p = strchr(s, '*');
		if (!p)
		{
			// No '*'; for now we assume that means it only works locally
			if (is_local)
				return strdup(s);
		}
		else
		if (node)
		{
			// Replace '*' with node
			const size_t nodelen = strlen(node), slen = strlen(s), ppos = p - s;
			char * const rv = malloc(slen + nodelen);
			memcpy(rv, s, ppos);
			memcpy(&rv[ppos], node, nodelen);
			memcpy(&rv[ppos+nodelen], &p[1], slen - (ppos + 1));
			rv[nodelen + slen - 1] = '\0';
			return rv;
		}
	}
	return NULL;
}

bool fabdcfg_zmq_connect(const char * const devid, const char * const clientname, void * const socket)
{
	json_t *j = fabdcfg_device_get(devid, "clients");
	if (!j)
		return false;
	j = json_object_get(j, clientname);
	if (!j)
		return false;
	j = fabd_json_array(j);
	bool success = true;
	for (size_t i = 0, il = json_array_size(j); i < il; ++i)
	{
		json_t * const ji = json_array_get(j, i);
		const char *s = json_string_value(ji);
		char *sfree = NULL;
		{
			char *dest_devid, *dest_servername;
			if (fabd_parse_devuri(s, &dest_devid, &dest_servername))
			{
				// Lookup server config and determine where to connect from that
				s = sfree = fabdcfg_server_get_connect(dest_devid, dest_servername, devid);
			}
		}
		
		if (zmq_connect(socket, s))
			success = false;
		
		free(sfree);
	}
	json_decref(j);
	return success;
}

