#include "config.h"

#include <assert.h>
#include <stdio.h>

#include <gpiod.h>
#include <zmq.h>

#include <freeabode/fabdcfg.h>
#include <freeabode/freeabode.pb-c.h>
#include <freeabode/json.h>
#include <freeabode/logging.h>
#include <freeabode/security.h>
#include <freeabode/util.h>
#include <freeabode/util_hvac.h>

static const struct timespec ts_shutoff_delay = { .tv_sec = 337, .tv_nsec = 500000000, };
static const struct timespec ts_reversing_delay_tolerance = { .tv_sec = 1, };

static const char *my_devid;
static void *my_zmq_context, *my_zmq_publisher;

struct my_gpioinfo {
	struct gpiod_line *gpioline;
	enum fabd_tristate value;
	struct timespec last_changed;
};

struct gpio_hvac_obj {
	struct my_gpioinfo gpio[PB_HVACWIRES___COUNT];
};

static
void gpio_hvac_obj_init(struct gpio_hvac_obj * const gho)
{
	struct timespec ts_now;
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	for (int i = 0; i < PB_HVACWIRES___COUNT; ++i)
	{
		gho->gpio[i].gpioline = NULL;
		gho->gpio[i].value = FTS_UNKNOWN;
		
		// Initialise last_changed to now, since it's used for safety lockouts only
		gho->gpio[i].last_changed = ts_now;
	}
}

static
struct my_gpioinfo *gpioinfo_from_wire(struct gpio_hvac_obj * const gho, const PbHVACWires wire) {
	if (wire >= PB_HVACWIRES___COUNT) return NULL;
	return &gho->gpio[wire];
}

static
bool control_wire_unsafe(struct gpio_hvac_obj * const gho, const PbHVACWires wire, const bool connect)
{
	struct my_gpioinfo * const gpioinfo = gpioinfo_from_wire(gho, wire);
	if (!gpioinfo->gpioline) return false;
	const bool success = (0 == gpiod_line_set_value(gpioinfo->gpioline, connect ? 1 : 0));
	if (!success) {
		applog(LOG_WARNING, "Failed to set GPIO for turning %s %s", connect ? "on" : "off", hvacwire_name(wire));
		return false;
	}
	
	if (gpioinfo->value != connect) {
		applog(LOG_INFO, "Turned %s %s", hvacwire_name(wire), connect ? "on" : "off");
		clock_gettime(CLOCK_MONOTONIC, &gpioinfo->last_changed);
	}
	gpioinfo->value = connect;
	
	PbEvent pbevent = PB_EVENT__INIT;
	PbSetHVACWireRequest pbwire = PB_SET_HVACWIRE_REQUEST__INIT;
	pbwire.wire = wire;
	pbwire.connect = connect;
	pbevent.wire_change = malloc(sizeof(*pbevent.wire_change));
	pbevent.n_wire_change = 1;
	*pbevent.wire_change = &pbwire;
	zmq_send_protobuf(my_zmq_publisher, pb_event, &pbevent, 0);
	free(pbevent.wire_change);
	
	return true;
}

static
bool control_wire_safe(struct gpio_hvac_obj * const gho, const PbHVACWires wire, const bool connect)
{
	struct my_gpioinfo * const gpioinfo = gpioinfo_from_wire(gho, wire);
	switch (wire) {
		case PB_HVACWIRES__Y1:  // compressor
		case PB_HVACWIRES__W2:  // heat 2
		{
			if (connect && gpioinfo->value != true) {
				// after turning off, lock off for a few minutes
				struct timespec ts_soonest_cycle, ts_now;
				timespec_add(&gpioinfo->last_changed, &ts_shutoff_delay, &ts_soonest_cycle);
				clock_gettime(CLOCK_MONOTONIC, &ts_now);
				if (timespec_cmp(&ts_now, &ts_soonest_cycle) < 0) {
					applog(LOG_WARNING, "Prevented attempt to turn on %s during safety lockout", hvacwire_name(wire));
					return false;
				}
				
				struct my_gpioinfo * const gpioinfo_fan = gpioinfo_from_wire(gho, PB_HVACWIRES__G);
				if (gpioinfo_fan->gpioline && gpioinfo_fan->value != true) {
					// force fan on
					if (!control_wire_safe(gho, PB_HVACWIRES__G, true)) {
						applog(LOG_WARNING, "Failed to force fan on during request to turn on %s", hvacwire_name(wire));
						return false;
					}
				}
			}
			break;
		}
		case PB_HVACWIRES__OB:  // reversing
		{
			// don't allow changes while compressor is running
			// but if attempted, try to shut off the compressor since it's working against what is presumably desired
			struct my_gpioinfo * const gpioinfo_compressor = gpioinfo_from_wire(gho, PB_HVACWIRES__Y1);
			if (gpioinfo_compressor->value != false && connect != gpioinfo->value) {
				struct timespec ts_tolerance, ts_now;
				timespec_add(&gpioinfo_compressor->last_changed, &ts_reversing_delay_tolerance, &ts_tolerance);
				clock_gettime(CLOCK_MONOTONIC, &ts_now);
				if (timespec_cmp(&ts_now, &ts_tolerance) > 0) {
					applog(LOG_WARNING, "Prevented attempt to turn %s reversing while compressor running", connect ? "on" : "off");
					control_wire_safe(gho, PB_HVACWIRES__Y1, false);
					return false;
				}
			}
			break;
		}
		case PB_HVACWIRES__G:   // fan
		{
			// don't allow turning off while compressor or heat running
			if (!connect) {
				struct my_gpioinfo * const gpioinfo_compressor = gpioinfo_from_wire(gho, PB_HVACWIRES__Y1);
				if (gpioinfo_compressor->value != false) {
					applog(LOG_WARNING, "Prevented attempt to turn off fan while compressor running");
					return false;
				}
				
				struct my_gpioinfo * const gpioinfo_heat2 = gpioinfo_from_wire(gho, PB_HVACWIRES__W2);
				if (gpioinfo_heat2->gpioline && gpioinfo_heat2->value != false) {
					applog(LOG_WARNING, "Prevented attempt to turn off fan while heat 2 running");
					return false;
				}
			}
			break;
		}
		default:
			// Assume unknown relays are always unsafe, since we have no safety controls
			applog(LOG_WARNING, "Prevented attempt to turn %s unknown wire #%d", connect ? "on" : "off", (int)wire);
			return false;
	}
	return control_wire_unsafe(gho, wire, connect);
}

void handle_req(void * const s, struct gpio_hvac_obj * const gho)
{
	PbRequest *req;
	zmq_recv_protobuf(s, pb_request, req, NULL);
	PbRequestReply reply = PB_REQUEST_REPLY__INIT;
	reply.n_sethvacwiresuccess = req->n_sethvacwire;
	reply.sethvacwiresuccess = malloc(sizeof(*reply.sethvacwiresuccess) * reply.n_sethvacwiresuccess);
	for (size_t i = 0; i < req->n_sethvacwire; ++i)
		reply.sethvacwiresuccess[i] = control_wire_safe(gho, req->sethvacwire[i]->wire, req->sethvacwire[i]->connect);
	pb_request__free_unpacked(req, NULL);
	zmq_send_protobuf(s, pb_request_reply, &reply, 0);
	free(reply.sethvacwiresuccess);
}

void got_new_subscriber(void * const s, struct gpio_hvac_obj * const gho)
{
	zmq_msg_t msg;
	assert(!zmq_msg_init(&msg));
	assert(zmq_msg_recv(&msg, s, 0) >= 0);
	if (zmq_msg_size(&msg) < 1)
		goto out;
	
	uint8_t * const data = zmq_msg_data(&msg);
	if (!data[0])
		goto out;
	
	PbEvent pbevent = PB_EVENT__INIT;
	
	PbSetHVACWireRequest *pbwire = malloc(sizeof(*pbwire) * PB_HVACWIRES___COUNT);
	PbSetHVACWireRequest *pbwire_top = pbwire;
	pbevent.wire_change = malloc(sizeof(*pbevent.wire_change) * PB_HVACWIRES___COUNT);
	pbevent.n_wire_change = 0;
	for (int i = 0; i < PB_HVACWIRES___COUNT; ++i)
	{
		const enum fabd_tristate asserted = gho->gpio[i].value;
		if (asserted == FTS_UNKNOWN)
			continue;
		
		pb_set_hvacwire_request__init(pbwire);
		pbwire->wire = i;
		pbwire->connect = asserted;
		
		pbevent.wire_change[pbevent.n_wire_change++] = pbwire;
		++pbwire;
	}
	
	zmq_send_protobuf(my_zmq_publisher, pb_event, &pbevent, 0);
	
	free(pbevent.wire_change);
	free(pbwire_top);
	
out:
	zmq_msg_close(&msg);
}

static
struct gpiod_line *fabd_get_gpiod_line(struct gpiod_chip * const gpiochip, const json_t * const json_gpios, const char * const key)
{
	const int gpio_num = fabd_json_as_int(json_object_get(json_gpios, key), -1);
	if (gpio_num < 0) return NULL;
	struct gpiod_line * const gpioline = gpiod_chip_get_line(gpiochip, gpio_num);
	assert(gpioline);
	assert(0 == gpiod_line_request_output(gpioline, "freeabode gpio_hvac", 0));
	return gpioline;
}

int main(int argc, char **argv)
{
	my_devid = fabd_common_argv(argc, argv, "gpio_hvac");
	load_freeabode_key();
	
	const char * const gpiochip_path = fabdcfg_device_getstr(my_devid, "gpiochip_device") ?: "/dev/gpiochip0";
	struct gpiod_chip * const gpiochip = gpiod_chip_open(gpiochip_path);
	assert(gpiochip);
	
	json_t * const json_gpios = fabdcfg_device_get(my_devid, "gpios");
	struct gpio_hvac_obj _gho, *gho = &_gho;
	gpio_hvac_obj_init(gho);
	gho->gpio[PB_HVACWIRES__Y1].gpioline = fabd_get_gpiod_line(gpiochip, json_gpios, "compressor");
	gho->gpio[PB_HVACWIRES__OB].gpioline = fabd_get_gpiod_line(gpiochip, json_gpios, "reversing");
	gho->gpio[PB_HVACWIRES__G ].gpioline = fabd_get_gpiod_line(gpiochip, json_gpios, "fan");
	gho->gpio[PB_HVACWIRES__W2].gpioline = fabd_get_gpiod_line(gpiochip, json_gpios, "heat 2");
	// TODO: Support other wires
	
	// Set them all to known and sane states (noop since Linux GPIO resets everything nowadays)
	control_wire_safe(gho, PB_HVACWIRES__W2, false);
	control_wire_safe(gho, PB_HVACWIRES__Y1, false);
	control_wire_safe(gho, PB_HVACWIRES__OB, false);
	control_wire_safe(gho, PB_HVACWIRES__G , false);
	
	my_zmq_context = zmq_ctx_new();
	start_zap_handler(my_zmq_context);
	
	void *my_zmq_ctl = zmq_socket(my_zmq_context, ZMQ_REP);
	freeabode_zmq_security(my_zmq_ctl, true);
	assert(fabdcfg_zmq_bind(my_devid, "control", my_zmq_ctl));
	
	my_zmq_publisher = zmq_socket(my_zmq_context, ZMQ_XPUB);
	zmq_setsockopt(my_zmq_publisher, ZMQ_XPUB_VERBOSE, &int_one, sizeof(int_one));
	freeabode_zmq_security(my_zmq_publisher, true);
	assert(fabdcfg_zmq_bind(my_devid, "events", my_zmq_publisher));
	
	struct timespec ts_now, ts_timeout;
	zmq_pollitem_t pollitems[] = {
		{ .socket = my_zmq_ctl, .events = ZMQ_POLLIN },
		{ .socket = my_zmq_publisher, .events = ZMQ_POLLIN },
	};
	while (true)
	{
		timespec_clear(&ts_timeout);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), timespec_to_timeout_ms(&ts_now, &ts_timeout)) <= 0)
			continue;
		if (pollitems[0].revents & ZMQ_POLLIN)
			handle_req(my_zmq_ctl, gho);
		if (pollitems[1].revents & ZMQ_POLLIN)
			got_new_subscriber(my_zmq_publisher, gho);
	}
}
