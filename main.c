#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <sodium/crypto_scalarmult.h>
#include <zmq.h>

#include "nest.h"
#include "freeabode.pb-c.h"
#include "zap.h"

static const int periodic_req_interval = 30;

static void *my_zmq_context, *my_zmq_publisher;
static const int int_one = 1;
static struct timespec ts_last_periodic_req;

static bytes_t freeabode__privkey = BYTES_INIT;
bytes_t freeabode__pubkey = BYTES_INIT;

static
bytes_t convert_private_key_to_public(const bytes_t privkey_in)
{
	bytes_t ret = BYTES_INIT;
	uint8_t privkey[32];
	if (bytes_len(&privkey_in) == 0x20)
		memcpy(privkey, bytes_buf(&privkey_in), 0x20);
	else
	{
		bytes_cpy(&ret, &privkey_in);
		bytes_nullterminate(&ret);
		if (!zmq_z85_decode(privkey, (char*)bytes_buf(&ret)))
		{
			bytes_free(&ret);
			return ret;
		}
	}
	bytes_resize(&ret, 0x28);
	crypto_scalarmult_base(bytes_buf(&ret), privkey);
	return ret;
}

void load_freeabode_key()
{
	if (bytes_len(&freeabode__privkey))
		return;
	
	FILE *F = fopen("secretkey", "r");
	assert(F);
	assert(!fseek(F, 0, SEEK_END));
	long sz = ftell(F);
	assert(sz >= 0);
	char ibuf[sz + 1];
	mlock(ibuf, sz);
	rewind(F);
	assert(1 == fread(ibuf, sz, 1, F));
	ibuf[sz] = '\0';
	fclose(F);
	
	bytes_t rv = BYTES_INIT;
	bytes_resize(&rv, 0x20);
	// Be careful not to cause realloc after mlock!
	void * const buf = bytes_buf(&rv);
	mlock(buf, 0x20);
	switch (sz)
	{
		case 0x20:
			memcpy(buf, ibuf, 0x20);
			break;
		case 0x28:
			assert(zmq_z85_decode(buf, ibuf));
			break;
		default:
			assert(0 && "Invalid private key size");
	}
	memset(ibuf, '\0', sz);
	munlock(ibuf, sz);
	
	bytes_free(&freeabode__privkey);
	freeabode__privkey = rv;
	// NOTE: rv is being copied directly, and should not be used anymore!
	
	freeabode__pubkey = convert_private_key_to_public(freeabode__privkey);
}

static
void request_periodic(struct nbp_device *nbp, const struct timespec *now)
{
	ts_last_periodic_req = *now;
	nbp_send(nbp, NBPM_REQ_PERIODIC, NULL, 0);
}

static
void reset_complete(struct nbp_device *nbp, const struct timespec *now, uint16_t fet_bitmask)
{
	nbp->cb_msg_fet_presence = NULL;
	printf("Backplate reset complete\n");
	
	request_periodic(nbp, now);
	
	my_zmq_context = zmq_ctx_new();
	
	start_zap_handler(my_zmq_context);
	
	my_zmq_publisher = zmq_socket(my_zmq_context, ZMQ_PUB);
	
	zmq_setsockopt(my_zmq_publisher, ZMQ_CURVE_SERVER, &int_one, sizeof(int_one));
	zmq_setsockopt(my_zmq_publisher, ZMQ_CURVE_SECRETKEY, bytes_buf(&freeabode__privkey), bytes_len(&freeabode__privkey));
	assert(!zmq_bind(my_zmq_publisher, "tcp://*:2929"));
	assert(!zmq_bind(my_zmq_publisher, "ipc://weather.ipc"));
}

void msg_log(struct nbp_device *nbp, const struct timespec *now, const char *msg)
{
	printf("Backplate: %s\n", msg);
}

void msg_weather(struct nbp_device *nbp, const struct timespec *now, uint16_t temperature, uint16_t humidity)
{
	int32_t fahrenheit = ((int32_t)temperature) * 90 / 5 + 32000;
	printf("Temperature %3d.%02d C (%4d.%03d F)    Humidity: %d.%d%%\n", temperature / 100, temperature % 100, fahrenheit / 1000, fahrenheit % 1000, humidity / 10, humidity % 10);
	
	PbWeather pb = PB_WEATHER__INIT;
	pb.has_temperature = true;
	pb.temperature = temperature;
	pb.has_humidity = true;
	pb.humidity = humidity;
	size_t pbsz = pb_weather__get_packed_size(&pb);
	uint8_t pbbuf[pbsz];
	pb_weather__pack(&pb, pbbuf);
	zmq_send(my_zmq_publisher, pbbuf, pbsz, 0);
}

int main(int argc, char **argv)
{
	load_freeabode_key();
	
	struct nbp_device *nbp = nbp_open("/dev/ttyO2");
	assert(nbp_send(nbp, NBPM_RESET, NULL, 0));
	nbp->cb_msg_fet_presence = reset_complete;
	nbp->cb_msg_log = msg_log;
	nbp->cb_msg_weather = msg_weather;
	
	struct timespec ts_now;
	zmq_pollitem_t pollitems[] = {
		{ .fd = nbp->_fd, .events = ZMQ_POLLIN },
	};
	while (true)
	{
		clock_gettime(CLOCK_MONOTONIC, &ts_now);
		if (ts_now.tv_sec - periodic_req_interval > ts_last_periodic_req.tv_sec)
			request_periodic(nbp, &ts_now);
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), -1) <= 0)
			continue;
		if (pollitems[0].revents & ZMQ_POLLIN)
			nbp_read(nbp);
	}
}
