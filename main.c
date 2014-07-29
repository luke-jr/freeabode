#include "config.h"

#include <assert.h>
#include <stdio.h>

#include "nest.h"

void reset_complete(struct nbp_device *nbp, const struct timespec *now, enum nbp_message_type mtype, const void *data, size_t datasz)
{
	nbp->cb_msg = NULL;
	printf("Backplate reset complete\n");
}

void msg_log(struct nbp_device *nbp, const struct timespec *now, const char *msg)
{
	printf("Backplate: %s\n", msg);
}

void msg_weather(struct nbp_device *nbp, const struct timespec *now, uint16_t temperature, uint16_t humidity)
{
	printf("Temperature %d.%02d C  Humidity: %d.%d\n", temperature / 100, temperature % 100, humidity / 10, humidity % 10);
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
