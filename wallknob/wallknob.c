#include <assert.h>
#include <pthread.h>
#include <stdbool.h>

#include <directfb.h>
#include <zmq.h>
#include <zmq_utils.h>

#include <freeabode/freeabode.pb-c.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

#define FONT_NAME  "DroidSansMono.ttf"

static IDirectFB *dfb;

static
void dfbassert(DFBResult err, const char *file, int line, const char *expr)
{
	if (err == DFB_OK)
		return;
	fprintf(stderr, "%s:%d: ", file, line);
	DirectFBErrorFatal(expr, err);
}
#define dfbassert(expr)  dfbassert(expr, __FILE__, __LINE__, #expr)

static void *my_zmq_context;

struct my_font {
	IDirectFBFont *dfbfont;
	int height;
	int descender;
};
struct my_font font_h2, font_h4;

static
void my_load_font(struct my_font * const myfont, const char * const fontname, const DFBFontDescription * const fontdsc)
{
	dfbassert(dfb->CreateFont(dfb, fontname, fontdsc, &myfont->dfbfont));
	dfbassert(myfont->dfbfont->GetHeight(myfont->dfbfont, &myfont->height));
	dfbassert(myfont->dfbfont->GetDescender(myfont->dfbfont, &myfont->descender));
}

static inline
int32_t decicelcius_to_millifahrenheit(int32_t dc)
{
	return dc * 90 / 5 + 32000;
}

static
void weather_thread(void * const userp)
{
	void *client_weather;
	client_weather = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(client_weather, false);
	assert(!zmq_connect(client_weather, "tcp://192.168.77.104:2929"));
	assert(!zmq_setsockopt(client_weather, ZMQ_SUBSCRIBE, NULL, 0));
	
	
	IDirectFBWindow * const window = userp;
	IDirectFBSurface *surface;
	dfbassert(window->GetSurface(window, &surface));
	int width, height;
	dfbassert(window->GetSize(window, &width, &height));
	dfbassert(surface->Clear(surface, 0, 0, 0, 0));
	dfbassert(surface->Flip(surface, NULL, DSFLIP_BLIT));
	dfbassert(window->SetOpacity(window, 0xff));
	
	char buf[0x10];
	bool fetstatus[PB_HVACWIRES___COUNT] = {true,true,true,true,true,true,true,true,true,true,true,true};
	int32_t fahrenheit = 0;
	while (true)
	{
		PbEvent *pbevent;
		zmq_recv_protobuf(client_weather, pb_event, pbevent, NULL);
		
		PbWeather *weather = pbevent->weather;
		if (weather && weather->has_temperature)
			fahrenheit = decicelcius_to_millifahrenheit(weather->temperature);
		if (pbevent->wire_change && pbevent->wire_change->wire < PB_HVACWIRES___COUNT && pbevent->wire_change->wire > 0)
			fetstatus[pbevent->wire_change->wire] = pbevent->wire_change->connect;
		
		snprintf(buf, sizeof(buf), "%2u %c%c%c", (unsigned)(fahrenheit / 1000), fetstatus[PB_HVACWIRES__Y1] ? 'Y' : ' ', fetstatus[PB_HVACWIRES__G] ? 'G' : ' ', fetstatus[PB_HVACWIRES__OB] ? 'O' : ' ');
		puts(buf);
		
		dfbassert(surface->Clear(surface, 0, 0xff, 0, 0x1f));
		dfbassert(surface->SetColor(surface, 0x80, 0xff, 0x20, 0xff));
		dfbassert(surface->SetFont(surface, font_h2.dfbfont));
		dfbassert(surface->DrawString(surface, buf, -1, width, font_h2.height, DSTF_RIGHT));
		dfbassert(surface->Flip(surface, NULL, DSFLIP_BLIT));
	}
}

static
void goal_thread(void * const userp)
{
	void *client_tstat;
	client_tstat = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(client_tstat, false);
	assert(!zmq_connect(client_tstat, "tcp://192.168.77.104:2931"));
	assert(!zmq_setsockopt(client_tstat, ZMQ_SUBSCRIBE, NULL, 0));
	
	IDirectFBWindow * const window = userp;
	IDirectFBSurface *surface;
	dfbassert(window->GetSurface(window, &surface));
	
	int width, height;
	dfbassert(window->GetSize(window, &width, &height));
	
	dfbassert(surface->Clear(surface, 0, 0, 0, 0));
	dfbassert(surface->Flip(surface, NULL, DSFLIP_BLIT));
	dfbassert(window->SetOpacity(window, 0xff));
	
	char buf[0x10];
	while (true)
	{
		PbEvent *pbevent;
		zmq_recv_protobuf(client_tstat, pb_event, pbevent, NULL);
		
		PbHVACGoals *goals = pbevent->hvacgoals;
		if (!(goals && goals->has_temp_high))
			continue;
		int fahrenheit = decicelcius_to_millifahrenheit(goals->temp_high);
		
		snprintf(buf, sizeof(buf), "%2u", (unsigned)(fahrenheit / 1000));
		
		dfbassert(surface->Clear(surface, 0, 0, 0xff, 0x1f));
		dfbassert(surface->SetColor(surface, 0x80, 0xff, 0x20, 0xff));
		dfbassert(surface->SetFont(surface, font_h4.dfbfont));
		dfbassert(surface->DrawString(surface, buf, -1, width, font_h4.height, DSTF_RIGHT));
		dfbassert(surface->Flip(surface, NULL, DSFLIP_BLIT));
	}
}

int main(int argc, char **argv)
{
	load_freeabode_key();
	my_zmq_context = zmq_ctx_new();
	
	IDirectFBDisplayLayer *layer;
	
	dfbassert(DirectFBInit(&argc, &argv));
	dfbassert(DirectFBCreate(&dfb));
	dfbassert(dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &layer));
	dfbassert(layer->SetCooperativeLevel(layer, DLSCL_ADMINISTRATIVE));
	dfbassert(layer->EnableCursor(layer, 0));
	dfbassert(layer->SetBackgroundMode(layer, DLBM_COLOR));
	dfbassert(layer->SetBackgroundColor(layer, 0, 0, 0, 0xff));
	IDirectFBSurface *surface;
	dfbassert(layer->GetSurface(layer, &surface));
	{
		int width, height;
		dfbassert(surface->GetSize(surface, &width, &height));
		const int center_x = width / 2;
		const int center_y = height / 2;
		
		DFBFontDescription font_dsc = {
			.flags = DFDESC_WIDTH,
			.width = width * 2 / 3 / 4,
		};
		my_load_font(&font_h2, FONT_NAME, &font_dsc);
		
		font_dsc.width /= 2;
		my_load_font(&font_h4, FONT_NAME, &font_dsc);
		
		IDirectFBWindow *window;
		DFBWindowDescription windesc = {
			.flags = DWDESC_CAPS | DWDESC_WIDTH | DWDESC_HEIGHT | DWDESC_POSX | DWDESC_POSY | DWDESC_OPTIONS,
			.caps = DWCAPS_ALPHACHANNEL,
			.width = width * 2 / 3,
			.height = font_h2.height - font_h2.descender,
			.posy = 0,
			.options = DWOP_ALPHACHANNEL,
		};
		windesc.posx = center_x - (windesc.width / 2);
		windesc.posy = height / 2;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		zmq_threadstart(weather_thread, window);
		
		windesc.height = font_h4.height - font_h4.descender;
		windesc.posy += font_h2.height;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		zmq_threadstart(goal_thread, window);
	}
	// TODO: handle input
	while (true)
		sleep(1);
}
