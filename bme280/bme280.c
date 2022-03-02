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
#include "driver/bme280.h"

static unsigned poll_interval_ms = 21094;

static void *zmq_pub;
static PbEvent current_pbe = PB_EVENT__INIT;
static PbWeather current_pbw = PB_WEATHER__INIT;

static int bme280_i2c_fd;

static
void handle_readings(const struct bme280_data * const data)
{
	const long temperature = data->temperature;
	const long humidity = (long)data->humidity * 10 / 1024;
	
	long fahrenheit = (temperature * 90 / 5) + 32000;
	applog(LOG_INFO, "Temperature %3ld.%02ld C (%4ld.%03ld F)  Humidity: %ld.%ld%%", temperature / 100, temperature % 100, fahrenheit / 1000, fahrenheit % 1000, humidity / 10, humidity % 10);
	
	current_pbw.has_temperature = true;
	current_pbw.temperature = temperature;
	current_pbw.has_humidity = true;
	current_pbw.humidity = humidity;
	zmq_send_protobuf(zmq_pub, pb_event, &current_pbe, 0);
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

static
void my_loop(const struct timespec * const ts_req_timeout, const int fd, const short fd_events)
{
	struct timespec ts_now, ts_timeout;
	zmq_pollitem_t pollitems[] = {
		{ .socket = zmq_pub, .events = ZMQ_POLLIN },
		{ .fd = fd, .events = fd_events },
	};
	int n_pollitems = (fd == -1) ? 1 : 2;
	while (true)
	{
		timespec_clear(&ts_timeout);
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (ts_req_timeout && timespec_passed(ts_req_timeout, &ts_now, &ts_timeout))
			return;
		if (pollitems[1].revents)
			return;
		if (zmq_poll(pollitems, n_pollitems, timespec_to_timeout_ms(&ts_now, &ts_timeout)) <= 0)
			continue;
		if (pollitems[0].revents & ZMQ_POLLIN)
			got_new_subscriber(zmq_pub);
	}
}

static
void my_delay_us(const uint32_t period, void * const intf_ptr)
{
	struct timespec ts_now, ts_timeout;
	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	timespec_add_ns(&ts_now, 1000 * (long long)period, &ts_timeout);
	my_loop(&ts_timeout, /*fd=*/-1, /*fd_events=*/0);
}

static
BME280_INTF_RET_TYPE my_i2c_write(const uint8_t register_addr, const uint8_t * const data, const uint32_t len, void * const intf_ptr)
{
	const uint8_t *p = &register_addr;
	size_t rem = 1;
	const int fd = bme280_i2c_fd;
	while (rem) {
		my_loop(NULL, fd, ZMQ_POLLOUT);
		const ssize_t rv = write(fd, p, rem);
		if (rv == rem) {
			if (p == &register_addr) {
				p = data;
				rem = len;
				continue;
			}
			break;
		}
		if (rv < 0) return !BME280_INTF_RET_SUCCESS;
		p += rv;
		rem -= rv;
	}
	return BME280_INTF_RET_SUCCESS;
}

static
BME280_INTF_RET_TYPE my_i2c_read(uint8_t register_addr, uint8_t * const data, const uint32_t len, void * const intf_ptr)
{
	if (BME280_INTF_RET_SUCCESS != my_i2c_write(register_addr, NULL, 0, intf_ptr))
		return !BME280_INTF_RET_SUCCESS;
	
	uint8_t *p = data;
	size_t rem = len;
	const int fd = bme280_i2c_fd;
	while (rem) {
		my_loop(NULL, fd, ZMQ_POLLIN);
		const ssize_t rv = read(fd, p, rem);
		if (rv == rem) break;
		if (rv < 0) return !BME280_INTF_RET_SUCCESS;
		p += rv;
		rem -= rv;
	}
	return BME280_INTF_RET_SUCCESS;
}

static
void my_init_bme280(struct bme280_dev * const dev)
{
	dev->intf = BME280_I2C_INTF;
	dev->read = my_i2c_read;
	dev->write = my_i2c_write;
	dev->delay_us = my_delay_us;
	assert(BME280_OK == bme280_init(dev));
	
	dev->settings.osr_h = BME280_OVERSAMPLING_1X;
	dev->settings.osr_p = BME280_OVERSAMPLING_16X;
	dev->settings.osr_t = BME280_OVERSAMPLING_2X;
	dev->settings.filter = BME280_FILTER_COEFF_16;
	const uint8_t settings_sel = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
	assert(BME280_OK == bme280_set_sensor_settings(settings_sel, dev));
}

int main(int argc, char **argv)
{
	const char * const devid = fabd_common_argv(argc, argv, "bme280");
	load_freeabode_key();
	
	const char * const i2cpath = fabdcfg_device_getstr(devid, "i2c_device") ?: "/dev/i2c-1";
	const int fd = open(i2cpath, O_RDWR);
	assert(fd >= 0);
	int addr;
	{
		const char * const addrstr = fabdcfg_device_getstr(devid, "i2c_address");
		addr = addrstr ? strtol(addrstr, NULL, 0) : 0x76;
	}
	assert(!ioctl(fd, I2C_SLAVE, addr));
	bme280_i2c_fd = fd;
	
	current_pbe.weather = &current_pbw;
	
	void * const zmq_ctx = zmq_ctx_new();
	start_zap_handler(zmq_ctx);
	
	zmq_pub = zmq_socket(zmq_ctx, ZMQ_XPUB);
	zmq_setsockopt(zmq_pub, ZMQ_XPUB_VERBOSE, &int_one, sizeof(int_one));
	freeabode_zmq_security(zmq_pub, true);
	assert(fabdcfg_zmq_bind(devid, "events", zmq_pub));
	
	struct bme280_dev _bme280, *bme280 = &_bme280;
	my_init_bme280(bme280);
	
	const uint32_t req_delay = bme280_cal_meas_delay(&bme280->settings);
	
	struct timespec ts_next_req;
	clock_gettime(CLOCK_MONOTONIC, &ts_next_req);
	
	struct bme280_data data;
	while (true)
	{
		my_loop(&ts_next_req, /*fd=*/-1, /*fd_events=*/0);
		
		if (BME280_OK != bme280_set_sensor_mode(BME280_FORCED_MODE, bme280)) {
			applog(LOG_ERR, "bme280_set_sensor_mode failed");
			goto schedule_next_poll;
		}
		my_delay_us(req_delay, bme280->intf_ptr);
		if (BME280_OK != bme280_get_sensor_data(BME280_TEMP | BME280_HUM, &data, bme280)) {
			applog(LOG_ERR, "bme280_get_sensor_data failed");
			goto schedule_next_poll;
		}
		
		handle_readings(&data);

schedule_next_poll:
		clock_gettime(CLOCK_MONOTONIC, &ts_next_req);
		timespec_add_ms(&ts_next_req, poll_interval_ms, &ts_next_req);
	}
}
