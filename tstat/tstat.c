#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include <zmq.h>

#include <freeabode/freeabode.pb-c.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

static const int default_temp_goal_high = 3020;
static const unsigned long fan_before_cool_ms =  10547;
static const unsigned long  fan_after_cool_ms =  42188;
static const unsigned long   shutoff_delay_ms = 337500;
static const unsigned long           retry_ms =   1319;
static const int temp_hysteresis = 10;

struct tstat_data {
	// ZMQ sockets
	void *client_hwctl;
	void *client_weather;
	void *server_events;
	void *server_ctl;
	
	// Configuration
	int t_goal_high;
	
	// State
	bool cooling;
	struct timespec ts_earliest_cool;
	
	// Timers
	struct timespec ts_turn_fan_on;
	struct timespec ts_turn_compressor_on;
	struct timespec ts_turn_fan_off;
};

static
void applog(const struct timespec *now, const char *fmt, ...)
{
	printf("%lu.%09ld ", now->tv_sec, now->tv_nsec);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	puts("");
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
void read_weather(struct tstat_data *tstat, struct timespec *ts_now)
{
	PbWeather *weather;
	zmq_recv_protobuf(tstat->client_weather, pb_weather, weather, NULL);
	
	if (weather->has_temperature)
	{
		applog(ts_now, "Temperature %2u.%02u C", (unsigned)(weather->temperature / 100), (unsigned)(weather->temperature % 100));
		if (tstat->cooling)
		{
			if (weather->temperature < tstat->t_goal_high - temp_hysteresis)
			{
				applog(ts_now, "No cool needed");
				if (timespec_isset(&tstat->ts_turn_fan_on))
				{
					// Fan hasn't turned on yet, just cancel it
					timespec_clear(&tstat->ts_turn_fan_on);
					tstat->cooling = false;
				}
				else
				if (timespec_isset(&tstat->ts_turn_compressor_on))
				{
					// Compressor hasn't turned on yet, just stop fan
					timespec_clear(&tstat->ts_turn_compressor_on);
					tstat->ts_turn_fan_off = *ts_now;
					tstat->cooling = false;
				}
				else
				{
					applog(ts_now, "Turning off compressor");
					bool success = true;
					success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, false);
					success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, false);
					if (!success)
						applog(ts_now, "WARNING: Failed to turn off compressor");
					else
					{
						tstat->cooling = false;
						struct timespec ts_now;
						clock_gettime(CLOCK_MONOTONIC, &ts_now);
						timespec_add_ms(&ts_now, fan_after_cool_ms, &tstat->ts_turn_fan_off);
					}
					timespec_add_ms(ts_now, shutoff_delay_ms, &tstat->ts_earliest_cool);
				}
			}
		}
		else
		{
			if (weather->temperature > tstat->t_goal_high + temp_hysteresis)
			{
				applog(ts_now, "Preparing to cool");
				tstat->cooling = true;
				if (timespec_isset(&tstat->ts_turn_fan_off))
				{
					// Fan wasn't turned off yet, go straight to compressor
					tstat->ts_turn_compressor_on = tstat->ts_earliest_cool;
					timespec_clear(&tstat->ts_turn_fan_off);
				}
				else
					tstat->ts_turn_fan_on = tstat->ts_earliest_cool;
			}
		}
	}
	
	pb_weather__free_unpacked(weather, NULL);
}

void handle_req(struct tstat_data *tstat)
{
	PbRequest *req;
	zmq_recv_protobuf(tstat->server_ctl, pb_request, req, NULL);
	PbRequestReply reply = PB_REQUEST_REPLY__INIT;
	
	// TODO
	
	pb_request__free_unpacked(req, NULL);
	zmq_send_protobuf(tstat->server_ctl, pb_request_reply, &reply, 0);
	free(reply.sethvacwiresuccess);
}

int main(int argc, char **argv)
{
	const char *myopt(int idx, const char *def)
	{
		if (idx >= argc)
			return def;
		return argv[idx];
	}
	
	load_freeabode_key();
	
	void *my_zmq_context;
	struct timespec ts_now, ts_timeout;
	struct tstat_data _tstat = {
		.t_goal_high = default_temp_goal_high,
		.ts_turn_fan_on = TIMESPEC_INIT_CLEAR,
		.ts_turn_compressor_on = TIMESPEC_INIT_CLEAR,
		.ts_turn_fan_off = TIMESPEC_INIT_CLEAR,
	}, *tstat = &_tstat;
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	{
		time_t t = time(NULL);
		applog(&ts_now, "%s", ctime(&t));
	}
	timespec_add_ms(&ts_now, shutoff_delay_ms, &tstat->ts_earliest_cool);
	
	my_zmq_context = zmq_ctx_new();
	
	tstat->client_hwctl = zmq_socket(my_zmq_context, ZMQ_REQ);
	freeabode_zmq_security(tstat->client_hwctl, false);
	assert(!zmq_connect(tstat->client_hwctl, myopt(1, "ipc://nbp.ipc")));
	
	tstat->client_weather = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(tstat->client_weather, false);
	assert(!zmq_connect(tstat->client_weather, myopt(2, "ipc://weather.ipc")));
	assert(!zmq_setsockopt(tstat->client_weather, ZMQ_SUBSCRIBE, NULL, 0));
	
	tstat->server_events = zmq_socket(my_zmq_context, ZMQ_PUB);
	freeabode_zmq_security(tstat->server_events, true);
	assert(!zmq_bind(tstat->server_events, "tcp://*:2931"));
	assert(!zmq_bind(tstat->server_events, "ipc://tstat-events.ipc"));
	
	tstat->server_ctl = zmq_socket(my_zmq_context, ZMQ_REP);
	freeabode_zmq_security(tstat->server_ctl, true);
	assert(!zmq_bind(tstat->server_ctl, "tcp://*:2932"));
	assert(!zmq_bind(tstat->server_ctl, "ipc://tstat-control.ipc"));
	
	zmq_pollitem_t pollitems[] = {
		{ .socket = tstat->client_weather, .events = ZMQ_POLLIN },
		{ .socket = tstat->server_ctl, .events = ZMQ_POLLIN },
	};
	while (true)
	{
		timespec_clear(&ts_timeout);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (timespec_passed(&tstat->ts_turn_fan_on, &ts_now, &ts_timeout))
		{
			applog(&ts_now, "Turning on  fan");
			if (hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G , true))
			{
				timespec_clear(&tstat->ts_turn_fan_on);
				timespec_add_ms(&ts_now, fan_before_cool_ms, &tstat->ts_turn_compressor_on);
			}
			else
			{
				applog(&ts_now, "FAILED to turn on fan");
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		if (timespec_passed(&tstat->ts_turn_compressor_on, &ts_now, &ts_timeout))
		{
			applog(&ts_now, "Turning on  compressor");
			bool success = true;
			success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, true);
			success &= hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, true);
			if (success)
				timespec_clear(&tstat->ts_turn_compressor_on);
			else
			{
				applog(&ts_now, "FAILED to turn on compressor");
				hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__Y1, false);
				hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__OB, false);
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		if (timespec_passed(&tstat->ts_turn_fan_off, &ts_now, &ts_timeout))
		{
			applog(&ts_now, "Turning off fan");
			timespec_add_ms(&ts_now, shutoff_delay_ms, &tstat->ts_earliest_cool);
			if (hvac_control_wire(tstat->client_hwctl, PB_HVACWIRES__G , false))
				timespec_clear(&tstat->ts_turn_fan_off);
			else
			{
				applog(&ts_now, "FAILED to turn off fan");
				timespec_add_ms(&ts_now, retry_ms, &ts_timeout);
			}
		}
		{
			char buf[3][0x100];
			timespec_to_str(buf[0], sizeof(buf[0]), &tstat->ts_earliest_cool);
			timespec_to_str(buf[1], sizeof(buf[1]), &tstat->ts_turn_fan_on);
			timespec_to_str(buf[2], sizeof(buf[2]), &tstat->ts_turn_compressor_on);
			timespec_to_str(buf[3], sizeof(buf[3]), &tstat->ts_turn_fan_off);
			applog(&ts_now, "Delay=%s FanOn=%s CompOn=%s FanOff=%s", buf[0], buf[1], buf[2], buf[3]);
		}
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), timespec_to_timeout_ms(&ts_now, &ts_timeout)) <= 0)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (pollitems[0].revents & ZMQ_POLLIN)
			read_weather(tstat, &ts_now);
		if (pollitems[1].revents & ZMQ_POLLIN)
			handle_req(tstat);
	}
}
