#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include <zmq.h>

#include <freeabode/fabdcfg.h>
#include <freeabode/freeabode.pb-c.h>
#include <freeabode/logging.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

static const int default_temp_goal_low  = 2400;
static const int default_temp_goal_high = 3020;
static const int default_temp_hysteresis = 50;
static const unsigned long fan_before_cool_ms =  10547;
static const unsigned long reversing_off_after_cool_ms = 5273;
static const unsigned long  fan_after_cool_ms =  42188;
static const unsigned long   shutoff_delay_ms = 337500;
static const unsigned long           retry_ms =   1319;

enum tstat_mode {
	TSM_OFF,
	TSM_COOL,
	TSM_HEAT,
};

struct tstat_zone_data {
	// ZMQ sockets
	void *client_weather;
	void *server_events;
	void *server_ctl;
	
	// Configuration
	int t_goal_low;
	int t_goal_high;
	int t_hysteresis;
	
	// State
	int t_latest;
};

struct tstat_data {
	// ZMQ sockets
	void *client_hwctl;
	
	// Configuration
	bool fan_always_on;
	bool do_not_start;
	
	// State
	enum tstat_mode mode;
	struct timespec ts_earliest_compressor;
	bool want_to_start;
	
	// Timers
	struct timespec ts_turn_fan_on;
	struct timespec ts_turn_compressor_on;
	struct timespec ts_turn_reversing_off;
	struct timespec ts_turn_fan_off;
	
	struct tstat_zone_data zone;
};

static
const char *tstat_mode_str(const enum tstat_mode mode)
{
	switch (mode)
	{
		case TSM_COOL:
			return "cool";
		case TSM_HEAT:
			return "heat";
		default:
		case TSM_OFF:
			return "off";
	}
}

static
void populate_pbevent_want_to_start(PbEvent * const pbevent, const struct tstat_data * const tstat)
{
	pbevent->has_want_to_start = true;
	pbevent->want_to_start = tstat->want_to_start;
}

static
bool hvac_control_wire(void *ctl, PbHVACWires wire, bool connect)
{
	PbRequest req = PB_REQUEST__INIT;
	void *mem = malloc(sizeof(*req.sethvacwire) + sizeof(*req.sethvacwire[0]));
	req.n_sethvacwire = 1;
	req.sethvacwire = mem;
	req.sethvacwire[0] = mem + sizeof(*req.sethvacwire);
	pb_set_hvacwire_request__init(req.sethvacwire[0]);
	req.sethvacwire[0]->wire = wire;
	req.sethvacwire[0]->connect = connect;
	zmq_send_protobuf(ctl, pb_request, &req, 0);
	free(mem);
	
	PbRequestReply *reply;
	zmq_recv_protobuf(ctl, pb_request_reply, reply, NULL);
	assert(reply->n_sethvacwiresuccess >= 1);
	bool rv = reply->sethvacwiresuccess[0];
	pb_request_reply__free_unpacked(reply, NULL);
	return rv;
}

static
bool tstat_set_fan_always_on(struct tstat_data * const tstat, const bool newvalue)
{
	if (tstat->fan_always_on == newvalue) return true;
	
	if (newvalue) {
		if (!hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G, true)) {
			applog(LOG_ERR, "FAILED to turn on fan");
			return false;
		}
		tstat->fan_always_on = true;
	} else {
		tstat->fan_always_on = false;
		if (!(tstat->mode != TSM_OFF || timespec_isset(&tstat->ts_turn_fan_off))) {
			if (!hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G, false)) {
				applog(LOG_ERR, "FAILED to turn off fan");
				return false;
			}
		}
	}
	return true;
}

static
void tstat_set_want_to_start(struct tstat_data * const tstat, const bool newvalue)
{
	if (tstat->want_to_start == newvalue) return;
	
	tstat->want_to_start = newvalue;
	
	PbEvent pbevent = PB_EVENT__INIT;
	populate_pbevent_want_to_start(&pbevent, tstat);
	zmq_send_protobuf(tstat->zone.server_events, pb_event, &pbevent, 0);
}

static
void do_compressor_off(struct tstat_data * const tstat, struct timespec * const ts_now)
{
	applog(LOG_INFO, "No %s needed", tstat_mode_str(tstat->mode));
	if (tstat->want_to_start) tstat_set_want_to_start(tstat, false);
	if (timespec_isset(&tstat->ts_turn_fan_on))
	{
		// Fan hasn't turned on yet, just cancel it
		timespec_clear(&tstat->ts_turn_fan_on);
		tstat->mode = TSM_OFF;
	}
	else
	if (timespec_isset(&tstat->ts_turn_compressor_on))
	{
		// Compressor hasn't turned on yet, just stop fan
		timespec_clear(&tstat->ts_turn_compressor_on);
		tstat->ts_turn_fan_off = *ts_now;
		tstat->mode = TSM_OFF;
	}
	else
	{
		applog(LOG_INFO, "Turning off compressor");
		bool success = true;
		success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, false);
		if (!success)
			applog(LOG_ERR, "WARNING: Failed to turn off compressor");
		else
		{
			tstat->mode = TSM_OFF;
			struct timespec ts_now;
			clock_gettime(CLOCK_MONOTONIC, &ts_now);
			timespec_add_ms(&ts_now, fan_after_cool_ms, &tstat->ts_turn_fan_off);
			timespec_add_ms(&ts_now, reversing_off_after_cool_ms, &tstat->ts_turn_reversing_off);
		}
		timespec_add_ms(ts_now, shutoff_delay_ms, &tstat->ts_earliest_compressor);
	}
}

static
void do_compressor_on(struct tstat_data * const tstat, const enum tstat_mode mode)
{
	assert(!timespec_isset(&tstat->ts_turn_reversing_off));
	applog(LOG_INFO, "Preparing to %s", tstat_mode_str(mode));
	tstat->mode = mode;
	if (timespec_isset(&tstat->ts_turn_fan_off))
	{
		// Fan wasn't turned off yet, go straight to compressor
		tstat->ts_turn_compressor_on = tstat->ts_earliest_compressor;
		timespec_clear(&tstat->ts_turn_fan_off);
	}
	else
		tstat->ts_turn_fan_on = tstat->ts_earliest_compressor;
}

static
void do_tstat_logic(struct tstat_data * const tstat, struct timespec * const ts_now)
{
	struct tstat_zone_data * const zone = &tstat->zone;
	switch (tstat->mode)
	{
		case TSM_COOL:
			if (zone->t_latest < zone->t_goal_high - zone->t_hysteresis) {
				do_compressor_off(tstat, ts_now);
			}
			break;
		case TSM_HEAT:
			if (zone->t_latest > zone->t_goal_low + zone->t_hysteresis) {
				do_compressor_off(tstat, ts_now);
			}
			break;
		case TSM_OFF:
			if (zone->t_latest > zone->t_goal_high + zone->t_hysteresis) {
				do_compressor_on(tstat, TSM_COOL);
			} else if (zone->t_latest < zone->t_goal_low - zone->t_hysteresis) {
				do_compressor_on(tstat, TSM_HEAT);
			}
			break;
	}
}

static
void read_weather(struct tstat_data * const tstat, struct timespec * const ts_now, struct tstat_zone_data * const zone)
{
	PbEvent *pbevent;
	zmq_recv_protobuf(zone->client_weather, pb_event, pbevent, NULL);
	PbWeather *weather = pbevent->weather;
	
	if (weather && weather->has_temperature)
	{
		applog(LOG_INFO, "Temperature %2u.%02u C", (unsigned)(weather->temperature / 100), (unsigned)(weather->temperature % 100));
		zone->t_latest = weather->temperature;
		do_tstat_logic(tstat, ts_now);
	}
	
	pb_event__free_unpacked(pbevent, NULL);
}

static
void populate_hvacgoals(PbHVACGoals * const goals, const struct tstat_data * const tstat, const struct tstat_zone_data * const zone)
{
	goals->has_temp_low = true;
	goals->temp_low = zone->t_goal_low;
	
	goals->has_temp_high = true;
	goals->temp_high = zone->t_goal_high;
	
	goals->has_temp_hysteresis = true;
	goals->temp_hysteresis = zone->t_hysteresis;
	
	goals->has_fan_mode = true;
	goals->fan_mode = (tstat->fan_always_on ? PB_FAN_MODE__AlwaysOn : PB_FAN_MODE__Auto);
	
	goals->has_do_not_start = true;
	goals->do_not_start = tstat->do_not_start;
}

void handle_req(struct tstat_data *tstat, struct tstat_zone_data * const zone)
{
	PbRequest *req;
	zmq_recv_protobuf(zone->server_ctl, pb_request, req, NULL);
	PbRequestReply reply = PB_REQUEST_REPLY__INIT;
	PbHVACGoals goalreply = PB_HVACGOALS__INIT;
	PbEvent pbevent = PB_EVENT__INIT;
	
	if (req->hvacgoals)
	{
		if (req->hvacgoals->has_temp_low)
			zone->t_goal_low = req->hvacgoals->temp_low;
		
		if (req->hvacgoals->has_temp_high)
			zone->t_goal_high = req->hvacgoals->temp_high;
		
		if (req->hvacgoals->has_temp_hysteresis)
			zone->t_hysteresis = req->hvacgoals->temp_hysteresis;
		
		if (req->hvacgoals->has_fan_mode)
			tstat_set_fan_always_on(tstat, (req->hvacgoals->fan_mode == PB_FAN_MODE__AlwaysOn));
		
		if (req->hvacgoals->has_do_not_start) {
			tstat->do_not_start = req->hvacgoals->do_not_start;
		}
		
		populate_hvacgoals(&goalreply, tstat, zone);
		reply.hvacgoals = &goalreply;
		pbevent.hvacgoals = &goalreply;
	}
	
	pb_request__free_unpacked(req, NULL);
	zmq_send_protobuf(zone->server_ctl, pb_request_reply, &reply, 0);
	zmq_send_protobuf(zone->server_events, pb_event, &pbevent, 0);
}

void got_new_subscriber(void * const s, const struct tstat_data * const tstat, const struct tstat_zone_data * const zone)
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
	PbHVACGoals goalreply = PB_HVACGOALS__INIT;
	populate_hvacgoals(&goalreply, tstat, zone);
	pbevent.hvacgoals = &goalreply;
	populate_pbevent_want_to_start(&pbevent, tstat);
	
	zmq_send_protobuf(s, pb_event, &pbevent, 0);
	
out:
	zmq_msg_close(&msg);
}

int main(int argc, char **argv)
{
	const char * const my_devid = fabd_common_argv(argc, argv, "tstat");
	load_freeabode_key();
	
	void *my_zmq_context;
	struct timespec ts_now, ts_timeout;
	struct tstat_data _tstat = {
		.ts_turn_fan_on = TIMESPEC_INIT_CLEAR,
		.ts_turn_compressor_on = TIMESPEC_INIT_CLEAR,
		.ts_turn_reversing_off = TIMESPEC_INIT_CLEAR,
		.ts_turn_fan_off = TIMESPEC_INIT_CLEAR,
	}, *tstat = &_tstat;
	
	tstat->zone = (struct tstat_zone_data){
		.t_goal_low = fabdcfg_device_getint(my_devid, "temp_low", default_temp_goal_low),
		.t_goal_high = fabdcfg_device_getint(my_devid, "temp_high", default_temp_goal_high),
		.t_hysteresis = fabdcfg_device_getint(my_devid, "temp_hysteresis", default_temp_hysteresis),
	};
	
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	timespec_add_ms(&ts_now, shutoff_delay_ms, &tstat->ts_earliest_compressor);
	
	my_zmq_context = zmq_ctx_new();
	
	tstat->client_hwctl = zmq_socket(my_zmq_context, ZMQ_REQ);
	freeabode_zmq_security(tstat->client_hwctl, false);
	assert(fabdcfg_zmq_connect(my_devid, "hwctl", tstat->client_hwctl));
	
	tstat->zone.client_weather = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(tstat->zone.client_weather, false);
	assert(fabdcfg_zmq_connect(my_devid, "weather", tstat->zone.client_weather));
	assert(!zmq_setsockopt(tstat->zone.client_weather, ZMQ_SUBSCRIBE, NULL, 0));
	
	tstat->zone.server_events = zmq_socket(my_zmq_context, ZMQ_XPUB);
	freeabode_zmq_security(tstat->zone.server_events, true);
	zmq_setsockopt(tstat->zone.server_events, ZMQ_XPUB_VERBOSE, &int_one, sizeof(int_one));
	assert(fabdcfg_zmq_bind(my_devid, "events", tstat->zone.server_events));
	
	tstat->zone.server_ctl = zmq_socket(my_zmq_context, ZMQ_REP);
	freeabode_zmq_security(tstat->zone.server_ctl, true);
	assert(fabdcfg_zmq_bind(my_devid, "control", tstat->zone.server_ctl));
	
	tstat_set_fan_always_on(tstat, fabdcfg_device_getbool(my_devid, "fan", false));
	
	zmq_pollitem_t pollitems[] = {
		{ .socket = tstat->zone.client_weather, .events = ZMQ_POLLIN },
		{ .socket = tstat->zone.server_ctl, .events = ZMQ_POLLIN },
		{ .socket = tstat->zone.server_events, .events = ZMQ_POLLIN },
	};
	while (true)
	{
		timespec_clear(&ts_timeout);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (timespec_passed(&tstat->ts_turn_fan_on, &ts_now, &ts_timeout))
		{
			if (tstat->do_not_start) goto handle_do_not_start;
			
			applog(LOG_INFO, "Turning on  fan");
			if (hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G , true))
			{
				timespec_clear(&tstat->ts_turn_fan_on);
				timespec_add_ms(&ts_now, fan_before_cool_ms, &tstat->ts_turn_compressor_on);
			}
			else
			{
				applog(LOG_ERR, "FAILED to turn on fan");
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		if (timespec_passed(&tstat->ts_turn_compressor_on, &ts_now, &ts_timeout))
		{
			if (tstat->do_not_start) goto handle_do_not_start;
			
			const bool ctl_ob = (tstat->mode == TSM_COOL);
			applog(LOG_INFO, "Turning on  compressor (OB=%c; mode=%s)", ctl_ob ? 'Y' : 'N', tstat_mode_str(tstat->mode));
			bool success = true;
			success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, ctl_ob);
			if (!success) {
				applog(LOG_ERR, "FAILED to turn %s reversing valve", ctl_ob ? "on " : "off");
				goto failed_reversing;
			}
			success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, true);
			if (success)
			{
				timespec_clear(&tstat->ts_turn_compressor_on);
				if (tstat->want_to_start) tstat_set_want_to_start(tstat, false);
			}
			else
			{
				applog(LOG_ERR, "FAILED to turn on compressor");
				hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, false);
failed_reversing:
				hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, false);
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		if (false) {
handle_do_not_start:
			if (!tstat->want_to_start) tstat_set_want_to_start(tstat, true);
		}
		if (timespec_passed(&tstat->ts_turn_reversing_off, &ts_now, &ts_timeout))
		{
			applog(LOG_INFO, "Turning off reversing valve");
			if (hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, false)) {
				timespec_clear(&tstat->ts_turn_reversing_off);
			} else {
				applog(LOG_ERR, "FAILED to turn off reversing valve");
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		if (timespec_passed(&tstat->ts_turn_fan_off, &ts_now, &ts_timeout))
		{
			if(tstat->fan_always_on) {
				timespec_clear(&tstat->ts_turn_fan_off);
				continue;
			}
			
			applog(LOG_INFO, "Turning off fan");
			timespec_add_ms(&ts_now, shutoff_delay_ms, &tstat->ts_earliest_compressor);
			if (hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G , false))
				timespec_clear(&tstat->ts_turn_fan_off);
			else
			{
				applog(LOG_ERR, "FAILED to turn off fan");
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		{
			char buf[5][0x100];
			timespec_to_str(buf[0], sizeof(buf[0]), &tstat->ts_earliest_compressor);
			timespec_to_str(buf[1], sizeof(buf[1]), &tstat->ts_turn_fan_on);
			timespec_to_str(buf[2], sizeof(buf[2]), &tstat->ts_turn_compressor_on);
			timespec_to_str(buf[3], sizeof(buf[3]), &tstat->ts_turn_fan_off);
			timespec_to_str(buf[4], sizeof(buf[4]), &tstat->ts_turn_reversing_off);
			applog(LOG_DEBUG, "Delay=%s FanOn=%s CompOn=%s FanOff=%s RevOff=%s", buf[0], buf[1], buf[2], buf[3], buf[4]);
		}
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), timespec_to_timeout_ms(&ts_now, &ts_timeout)) <= 0)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (pollitems[0].revents & ZMQ_POLLIN)
			read_weather(tstat, &ts_now, &tstat->zone);
		if (pollitems[1].revents & ZMQ_POLLIN)
			handle_req(tstat, &tstat->zone);
		if (pollitems[2].revents & ZMQ_POLLIN)
			got_new_subscriber(tstat->zone.server_events, tstat, &tstat->zone);
	}
}
