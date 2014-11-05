#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>

#include <zmq.h>

#include <freeabode/fabdcfg.h>
#include <freeabode/freeabode.pb-c.h>
#include <freeabode/logging.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

static unsigned poll_interval_ms = 21094;

static void *zmq_pub;
static PbEvent current_pbe = PB_EVENT__INIT;
static PbWeather current_pbw = PB_WEATHER__INIT;

typedef bool req_func_t(int, const struct timespec *);
static req_func_t *req_func;
static struct timespec ts_next_req;

static req_func_t htu21d_req_temp;
static req_func_t htu21d_rcv_temp;
static req_func_t htu21d_req_humid;
static req_func_t htu21d_rcv_humid;

static
bool poll_complete(const struct timespec * const now)
{
	req_func = htu21d_req_temp;
	timespec_add_ms(now, poll_interval_ms, &ts_next_req);
	return true;
}

static
void htu21d_reset(const int fd, const struct timespec *now)
{
	if (1 != write(fd, "\xfe", 1))
		applog(LOG_ERR, "Failed to soft reset HTU21D");
	poll_complete(now);
}

static
bool htu21d_req_temp(const int fd, const struct timespec *now)
{
	if (1 != write(fd, "\xf3", 1))
		return false;
	
	req_func = htu21d_rcv_temp;
	timespec_add_ms(now, 50, &ts_next_req);
	return true;
}

static
bool htu21d_rcv_temp(const int fd, const struct timespec *now)
{
	char buf[2];
	if (2 != read(fd, buf, 2))
		return false;
	if (buf[1] & 2)
		// Indicates a humidity reading
		return false;
	
	long temperature = ((unsigned)buf[0] << 8) | (buf[1] & 0xfc);
	temperature = (temperature * 17572 / 0x10000) - 4685;
	
	long fahrenheit = (temperature * 90 / 5) + 32000;
	applog(LOG_INFO, "Temperature %3ld.%02ld C (%4ld.%03ld F)", temperature / 100, temperature % 100, fahrenheit / 1000, fahrenheit % 1000);
	
	PbEvent pbe = PB_EVENT__INIT;
	PbWeather pbw = PB_WEATHER__INIT;
	current_pbw.has_temperature = pbw.has_temperature = true;
	current_pbw.temperature = pbw.temperature = temperature;
	pbe.weather = &pbw;
	zmq_send_protobuf(zmq_pub, pb_event, &pbe, 0);
	
	htu21d_req_humid(fd, now);
	return true;
}

static
bool htu21d_req_humid(const int fd, const struct timespec *now)
{
	if (1 != write(fd, "\xf5", 1))
		return false;
	
	req_func = htu21d_rcv_humid;
	timespec_add_ms(now, 16, &ts_next_req);
	return true;
}

static
bool htu21d_rcv_humid(const int fd, const struct timespec *now)
{
	char buf[2];
	if (2 != read(fd, buf, 2))
		return false;
	if (!(buf[1] & 2))
		// Indicates a temperature reading
		return false;
	
	long humidity = ((unsigned)buf[0] << 8) | (buf[1] & 0xfc);
	humidity = (humidity * 1250 / 0x10000) - 60;
	applog(LOG_INFO, "Humidity: %ld.%ld%%", humidity / 10, humidity % 10);
	
	PbEvent pbe = PB_EVENT__INIT;
	PbWeather pbw = PB_WEATHER__INIT;
	current_pbw.has_humidity = pbw.has_humidity = true;
	current_pbw.humidity = pbw.humidity = humidity;
	pbe.weather = &pbw;
	zmq_send_protobuf(zmq_pub, pb_event, &pbe, 0);
	
	return poll_complete(now);
}

void got_new_subscriber(void * const s)
{
	zmq_msg_t msg;
	assert(!zmq_msg_init(&msg));
	assert(zmq_msg_recv(&msg, s, 0) >= 0);
	if (zmq_msg_size(&msg) < 1)
		goto out;
	
	uint8_t * const data = zmq_msg_data(&msg);
	if (!data[0])
		goto out;
	
	zmq_send_protobuf(zmq_pub, pb_event, &current_pbe, 0);
	
out:
	zmq_msg_close(&msg);
}

int main(int argc, char **argv)
{
	const char * const devid = fabd_common_argv(argc, argv, "htu21d");
	load_freeabode_key();
	
	const char * const i2cpath = fabdcfg_device_getstr(devid, "i2c_device") ?: "/dev/i2c-1";
	const int fd = open(i2cpath, O_RDWR);
	assert(fd >= 0);
	int addr;
	{
		const char * const addrstr = fabdcfg_device_getstr(devid, "i2c_address");
		addr = addrstr ? strtol(addrstr, NULL, 0) : 0x40;
	}
	assert(!ioctl(fd, I2C_SLAVE, addr));
	
	current_pbe.weather = &current_pbw;
	
	void * const zmq_ctx = zmq_ctx_new();
	start_zap_handler(zmq_ctx);
	
	zmq_pub = zmq_socket(zmq_ctx, ZMQ_XPUB);
	zmq_setsockopt(zmq_pub, ZMQ_XPUB_VERBOSE, &int_one, sizeof(int_one));
	freeabode_zmq_security(zmq_pub, true);
	assert(fabdcfg_zmq_bind(devid, "events", zmq_pub));
	
	req_func = htu21d_req_temp;
	clock_gettime(CLOCK_MONOTONIC, &ts_next_req);
	
	struct timespec ts_now, ts_timeout;
	zmq_pollitem_t pollitems[] = {
		{ .socket = zmq_pub, .events = ZMQ_POLLIN },
	};
	while (true)
	{
		timespec_clear(&ts_timeout);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
next_req:
		if (timespec_passed(&ts_next_req, &ts_now, &ts_timeout))
		{
			if (!req_func(fd, &ts_now))
				htu21d_reset(fd, &ts_now);
			// Need to get new time in ts_timeout
			goto next_req;
		}
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), timespec_to_timeout_ms(&ts_now, &ts_timeout)) <= 0)
			continue;
		if (pollitems[0].revents & ZMQ_POLLIN)
			got_new_subscriber(zmq_pub);
	}
}
