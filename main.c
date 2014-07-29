#include "config.h"

#include <assert.h>
#include <stdio.h>

#include <zmq.h>

#include "nest.h"
#include "freeabode.pb-c.h"

static void *my_zmq_context, *my_zmq_publisher;

void reset_complete(struct nbp_device *nbp, const struct timespec *now, enum nbp_message_type mtype, const void *data, size_t datasz)
{
	nbp->cb_msg = NULL;
	printf("Backplate reset complete\n");
	my_zmq_context = zmq_ctx_new();
	my_zmq_publisher = zmq_socket(my_zmq_context, ZMQ_PUB);
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
	struct nbp_device *nbp = nbp_open("/dev/ttyO2");
	assert(nbp_send(nbp, NBPM_RESET, NULL, 0));
	nbp->cb_msg = reset_complete;
	nbp->cb_msg_log = msg_log;
	nbp->cb_msg_weather = msg_weather;
	while (true)
		nbp_read(nbp);
}
