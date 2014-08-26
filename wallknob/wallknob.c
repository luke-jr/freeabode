#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>

#include <directfb.h>
#include <zmq.h>
#include <zmq_utils.h>

#include <freeabode/freeabode.pb-c.h>
#include <freeabode/security.h>
#include <freeabode/util.h>

#define ADJUSTMENT_DELAY_SECS  1, 318
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
	int ascender;
	int descender;
	int width_x;
};
struct my_font font_h2, font_h4;

static
void my_load_font(struct my_font * const myfont, const char * const fontname, const DFBFontDescription * const fontdsc)
{
	dfbassert(dfb->CreateFont(dfb, fontname, fontdsc, &myfont->dfbfont));
	dfbassert(myfont->dfbfont->GetHeight(myfont->dfbfont, &myfont->height));
	dfbassert(myfont->dfbfont->GetAscender(myfont->dfbfont, &myfont->ascender));
	dfbassert(myfont->dfbfont->GetDescender(myfont->dfbfont, &myfont->descender));
	dfbassert(myfont->dfbfont->GetStringWidth(myfont->dfbfont, "x", 1, &myfont->width_x));
}

struct my_window_info {
	IDirectFBWindow *win;
	IDirectFBSurface *surface;
	DFBDimension sz;
};

static
void my_win_init(struct my_window_info * const wininfo)
{
	dfbassert(wininfo->win->GetSurface(wininfo->win, &wininfo->surface));
	dfbassert(wininfo->win->GetSize(wininfo->win, &wininfo->sz.w, &wininfo->sz.h));
	dfbassert(wininfo->surface->Clear(wininfo->surface, 0, 0, 0, 0));
	dfbassert(wininfo->surface->Flip(wininfo->surface, NULL, DSFLIP_BLIT));
	dfbassert(wininfo->win->SetOpacity(wininfo->win, 0xff));
}

static inline
int32_t decicelcius_to_millifahrenheit(int32_t dc)
{
	return dc * 90 / 5 + 32000;
}

struct weather_windows {
	struct my_window_info temp;
	struct my_window_info tempgoal;
	struct my_window_info humid;
	struct my_window_info i_hvac;
	struct my_window_info i_charging;
	struct my_window_info circle;
};

static
void update_win_temp(struct my_window_info * const wi, const int32_t decicelcius)
{
	char buf[0x10];
	int32_t fahrenheit = decicelcius_to_millifahrenheit(decicelcius);
	snprintf(buf, sizeof(buf), "%2u", (unsigned)(fahrenheit / 1000));
	
	dfbassert(wi->surface->Clear(wi->surface, 0, 0xff, 0, 0x1f));
	dfbassert(wi->surface->SetColor(wi->surface, 0x80, 0xff, 0x20, 0xff));
	dfbassert(wi->surface->SetFont(wi->surface, font_h2.dfbfont));
	dfbassert(wi->surface->DrawString(wi->surface, buf, -1, wi->sz.w, font_h2.height, DSTF_RIGHT));
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void update_win_tempgoal(struct my_window_info * const wi, int32_t current, int32_t adjusted)
{
	char buf[0x10];
	
	current  = decicelcius_to_millifahrenheit(current ) / 1000;
	adjusted = decicelcius_to_millifahrenheit(adjusted) / 1000;
	
	snprintf(buf, sizeof(buf), "%2u", (unsigned)adjusted);
	
	dfbassert(wi->surface->Clear(wi->surface, 0, 0, 0xff, 0x1f));
	if (adjusted == current)
		dfbassert(wi->surface->SetColor(wi->surface, 0x80, 0xff, 0x20, 0xff));
	else
		dfbassert(wi->surface->SetColor(wi->surface, 0xff, 0x80, 0x20, 0xff));
	dfbassert(wi->surface->SetFont(wi->surface, font_h4.dfbfont));
	dfbassert(wi->surface->DrawString(wi->surface, buf, -1, wi->sz.w, font_h4.height, DSTF_RIGHT));
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void update_win_humid(struct my_window_info * const wi, const unsigned humidity)
{
	char buf[0x10];
	snprintf(buf, sizeof(buf), "%02u", humidity / 10);
	
	dfbassert(wi->surface->Clear(wi->surface, 0xff, 0, 0, 0x1f));
	dfbassert(wi->surface->SetColor(wi->surface, 0x80, 0xff, 0x20, 0xff));
	dfbassert(wi->surface->SetFont(wi->surface, font_h2.dfbfont));
	dfbassert(wi->surface->DrawString(wi->surface, buf, -1, 0, font_h2.height, DSTF_LEFT));
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void update_win_i_charging(struct my_window_info * const wi, const bool charging)
{
	dfbassert(wi->surface->Clear(wi->surface, 0, 0, 0xff, 0x1f));
	if (charging)
	{
		dfbassert(wi->surface->SetColor(wi->surface, 0x80, 0xff, 0x20, 0xff));
		dfbassert(wi->surface->SetFont(wi->surface, font_h4.dfbfont));
		dfbassert(wi->surface->DrawString(wi->surface, "Charging", -1, wi->sz.w / 2, font_h4.height, DSTF_CENTER));
	}
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void update_win_i_hvac(struct my_window_info * const wi, const bool * const fetstatus)
{
	char buf[0x10];
	if (fetstatus[PB_HVACWIRES__Y1])
		snprintf(buf, sizeof(buf), "%s%s", fetstatus[PB_HVACWIRES__OB] ? "Cool" : "Heat", fetstatus[PB_HVACWIRES__G] ? "" : " (no fan)");
	else
	if (fetstatus[PB_HVACWIRES__G])
		strcpy(buf, "Fan");
	else
		strcpy(buf, "Off");
	
	dfbassert(wi->surface->Clear(wi->surface, 0xff, 0, 0, 0x1f));
	dfbassert(wi->surface->SetColor(wi->surface, 0x80, 0xff, 0x20, 0xff));
	dfbassert(wi->surface->SetFont(wi->surface, font_h4.dfbfont));
	dfbassert(wi->surface->DrawString(wi->surface, buf, -1, wi->sz.w / 2, font_h4.height, DSTF_CENTER));
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void my_draw_tick(IDirectFBSurface * const surface, const DFBPoint center, const double r1, const double r2, const double thickness, const double radian)
{
	double rx1, ry1, rx2, ry2;
	
	{
		double x1, y1, x2, y2;
		
		rx1 = cos(radian);
		ry1 = sin(radian);
		x1 = center.x + r1 * rx1;
		y1 = center.y + r1 * ry1;
		x2 = center.x + r2 * rx1;
		y2 = center.y + r2 * ry1;
		
		dfbassert(surface->DrawLine(surface, x1, y1, x2, y2));
	}
	
	if (thickness <= 0)
		return;
	
	const double thickness_2 = thickness / 2;
	
	rx1 = cos(radian - thickness_2);
	ry1 = sin(radian - thickness_2);
	rx2 = cos(radian + thickness_2);
	ry2 = sin(radian + thickness_2);
	
	DFBTriangle t[2];
	t[0] = (DFBTriangle){
		.x1 = center.x + r1 * rx1,
		.y1 = center.y + r1 * ry1,
		.x2 = center.x + r1 * rx2,
		.y2 = center.y + r1 * ry2,
		.x3 = center.x + r2 * rx1,
		.y3 = center.y + r2 * ry1,
	};
	t[1] = (DFBTriangle){
		.x1 = t[0].x2,
		.y1 = t[0].y2,
		.x2 = t[0].x3,
		.y2 = t[0].y3,
		.x3 = center.x + r2 * rx2,
		.y3 = center.y + r2 * ry2,
	};
	dfbassert(surface->FillTriangles(surface, t, sizeof(t) / sizeof(*t)));
}

static
void my_draw_coloured_tick(IDirectFBSurface * const surface, const DFBPoint center, const double r1, const double r2, const double thickness, const double i, const int units_around, const double radians_per_unit, const double radian_offset)
{
	int red, blue;
	red  = fabd_min(0xff, 0xff * i / (units_around / 2));
	blue = fabd_min(0xff, 0xff * (units_around - i - 1) / (units_around / 2));
	dfbassert(surface->SetColor(surface, red, 0, blue, 0xff));
	my_draw_tick(surface, center, r1, r2, thickness, radians_per_unit * i + radian_offset);
}

static
double my_temp_to_unit(const double temp, const double units_min, const double units_around)
{
	double r = ((double)decicelcius_to_millifahrenheit(temp)) / 1000. - units_min;
	r = fmin(units_around, fmax(0, r));
	return r;
}

static
void update_win_circle(struct my_window_info * const wi, const int32_t current_temp, const int current_goal, const int adjusted_goal)
{
	const double radians_around = (M_PI * 2) - 1;
	const double radians_omit = (M_PI * 2) - radians_around;
	const double radians_omit_div2 = radians_omit / 2;
	const int units_around = 50;
	const double radians_per_unit = radians_around / (units_around - 1);
	const double radian_offset = M_PI_2 + radians_omit_div2;
	const int units_min = 40;
	const int units_max = units_min + units_around;
	
	double r1 = wi->sz.w / 2;
	double r2 = r1 - (r1 / 8);
	double r3 = r2 - ((r1 - r2) / 2);
	DFBPoint center = {
		.x = wi->sz.w / 2,
		.y = wi->sz.h / 2,
	};
	
	dfbassert(wi->surface->Clear(wi->surface, 0xff, 0xff, 0xff, 0));
	wi->surface->SetRenderOptions(wi->surface, DSRO_ANTIALIAS);
	
	{
		double adjusted_goal_radian = my_temp_to_unit(adjusted_goal, units_min, units_around);
		adjusted_goal_radian = (adjusted_goal_radian * radians_per_unit) + radian_offset;
		dfbassert(wi->surface->SetColor(wi->surface, 0xff, 0xff, 0xff, 0xcf));
		// FIXME: Use the hysteresis as width
		my_draw_tick(wi->surface, center, r1, r3, radians_per_unit * 2, adjusted_goal_radian);
	}
	
	{
		double current_temp_unit = my_temp_to_unit(current_temp, units_min, units_around);
		my_draw_coloured_tick(wi->surface, center, r1, r3, radians_per_unit / 4, current_temp_unit, units_around, radians_per_unit, radian_offset);
	}
	
	for (int i = 0; i < units_around; ++i)
		my_draw_coloured_tick(wi->surface, center, r1, r2, radians_per_unit / 8, i, units_around, radians_per_unit, radian_offset);
	
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

static
void weather_recv(struct weather_windows * const ww, void * const client_weather, int32_t * const current_temp_p)
{
	static bool fetstatus[PB_HVACWIRES___COUNT] = {true,true,true,true,true,true,true,true,true,true,true,true};
	
	PbEvent *pbevent;
	zmq_recv_protobuf(client_weather, pb_event, pbevent, NULL);
	
	PbWeather *weather = pbevent->weather;
	if (weather)
	{
		if (weather->has_temperature)
		{
			*current_temp_p = weather->temperature;
			update_win_temp(&ww->temp, weather->temperature);
		}
		if (weather->has_humidity)
			update_win_humid(&ww->humid, weather->humidity);
	}
	if (pbevent->n_wire_change)
	{
		for (int i = 0; i < pbevent->n_wire_change; ++i)
			if (pbevent->wire_change[i]->wire < PB_HVACWIRES___COUNT && pbevent->wire_change[i]->wire > 0)
				fetstatus[pbevent->wire_change[i]->wire] = pbevent->wire_change[i]->connect;
		update_win_i_hvac(&ww->i_hvac, fetstatus);
	}
	if (pbevent->battery && pbevent->battery->has_charging)
		update_win_i_charging(&ww->i_charging, pbevent->battery->charging);
}

static int current_goal;
static bool adjusting;
static int adjusted_goal;
static int adjusting_pipe[2];
static void *client_tstat_ctl;

static
void init_client_tstat_ctl()
{
	client_tstat_ctl = zmq_socket(my_zmq_context, ZMQ_REQ);
	freeabode_zmq_security(client_tstat_ctl, false);
	assert(!zmq_connect(client_tstat_ctl, "tcp://192.168.77.104:2932"));
}

static
void tstat_recv(struct weather_windows * const ww, void * const client_tstat)
{
	PbEvent *pbevent;
	zmq_recv_protobuf(client_tstat, pb_event, pbevent, NULL);
	
	PbHVACGoals *goals = pbevent->hvacgoals;
	if (goals && goals->has_temp_high)
	{
		current_goal = goals->temp_high;
		if (!client_tstat_ctl)
			init_client_tstat_ctl();
		if (!adjusting)
			update_win_tempgoal(&ww->tempgoal, current_goal, current_goal);
	}
}

static
void weather_thread(void * const userp)
{
	struct weather_windows * const ww = userp;
	
	void *client_tstat;
	client_tstat = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(client_tstat, false);
	assert(!zmq_connect(client_tstat, "tcp://192.168.77.104:2931"));
	assert(!zmq_setsockopt(client_tstat, ZMQ_SUBSCRIBE, NULL, 0));
	
	void *client_weather;
	client_weather = zmq_socket(my_zmq_context, ZMQ_SUB);
	freeabode_zmq_security(client_weather, false);
	assert(!zmq_connect(client_weather, "tcp://192.168.77.104:2929"));
	assert(!zmq_setsockopt(client_weather, ZMQ_SUBSCRIBE, NULL, 0));
	
	my_win_init(&ww->temp);
	my_win_init(&ww->tempgoal);
	my_win_init(&ww->i_hvac);
	my_win_init(&ww->i_charging);
	my_win_init(&ww->humid);
	my_win_init(&ww->circle);
	
	zmq_pollitem_t pollitems[] = {
		{ .socket = client_tstat, .events = ZMQ_POLLIN },
		{ .fd = adjusting_pipe[0], .events = ZMQ_POLLIN },
		{ .socket = client_weather, .events = ZMQ_POLLIN },
	};
	
	char buf[0x10];
	int32_t current_temp = 0;
	while (true)
	{
		if (zmq_poll(pollitems, sizeof(pollitems) / sizeof(*pollitems), -1) <= 0)
			continue;
		
		if (pollitems[0].revents & ZMQ_POLLIN)
			tstat_recv(ww, client_tstat);
		if (pollitems[1].revents & ZMQ_POLLIN)
		{
			read(adjusting_pipe[0], buf, sizeof(buf));
			update_win_tempgoal(&ww->tempgoal, current_goal, adjusting ? adjusted_goal : current_goal);
		}
		if (pollitems[2].revents & ZMQ_POLLIN)
			weather_recv(ww, client_weather, &current_temp);
		
		update_win_circle(&ww->circle, current_temp, current_goal, adjusting ? adjusted_goal : current_goal);
	}
}

// right is negative, left is positive
static
void handle_knob_turn(const int axisrel)
{
	if (!client_tstat_ctl)
		return;
	if (!adjusting)
	{
		adjusting = true;
		adjusted_goal = current_goal;
	}
	adjusted_goal -= axisrel;
	write(adjusting_pipe[1], "", 1);
}

static
void make_adjustments()
{
	{
		PbRequest req = PB_REQUEST__INIT;
		PbHVACGoals goals = PB_HVACGOALS__INIT;
		goals.has_temp_high = true;
		goals.temp_high = adjusted_goal;
		req.hvacgoals = &goals;
		zmq_send_protobuf(client_tstat_ctl, pb_request, &req, 0);
	}
	
	{
		PbRequestReply *reply;
		zmq_recv_protobuf(client_tstat_ctl, pb_request_reply, reply, NULL);
		bool success = false;
		if (reply->hvacgoals && reply->hvacgoals->has_temp_high)
		{
			current_goal = reply->hvacgoals->temp_high;
			success = (reply->hvacgoals->temp_high == adjusted_goal);
				success = true;
		}
		pb_request_reply__free_unpacked(reply, NULL);
		if (!success)
			return;
	}
	
	adjusting = false;
	write(adjusting_pipe[1], "", 1);
}

static
void handle_button_press()
{
	// TODO
}

int main(int argc, char **argv)
{
	load_freeabode_key();
	my_zmq_context = zmq_ctx_new();
	assert(!pipe(adjusting_pipe));
	
	IDirectFBDisplayLayer *layer;
	struct weather_windows weather_windows;
	
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
			.width = (width * 2 / 3 / 2) - (font_h2.width_x / 2),
			.height = font_h2.height - font_h2.descender,
			.posy = 0,
			.options = DWOP_ALPHACHANNEL,
		};
		windesc.posx = center_x - (windesc.width + font_h2.width_x / 2);
		windesc.posy = (height / 2) - (font_h2.height - font_h2.ascender);
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows = (struct weather_windows){
			.temp.win = window,
		};
		
		windesc.posx += windesc.width + font_h2.width_x;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows.humid.win = window;
		windesc.posx -= windesc.width + font_h2.width_x;
		
		windesc.height = font_h4.height - font_h4.descender;
		windesc.posy += font_h2.height;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows.tempgoal.win = window;
		
		windesc.width = width * 2 / 3;
		windesc.posx = center_x - (windesc.width / 2);
		windesc.posy += font_h4.height;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows.i_hvac.win = window;
		
		windesc.posy += font_h4.height;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows.i_charging.win = window;
		
		windesc.posx = windesc.posy = 0;
		windesc.width = width;
		windesc.height = height;
		dfbassert(layer->CreateWindow(layer, &windesc, &window));
		weather_windows.circle.win = window;
		
		zmq_threadstart(weather_thread, &weather_windows);
	}
	
	// Main thread now handles input
	IDirectFBEventBuffer *evbuf;
	DFBEvent ev;
	dfbassert(dfb->CreateInputEventBuffer(dfb, DICAPS_ALL, DFB_TRUE, &evbuf));
	while (true)
	{
		{
			DFBResult res;
			if (adjusting)
			{
				res = evbuf->WaitForEventWithTimeout(evbuf, ADJUSTMENT_DELAY_SECS);
				if (res == DFB_TIMEOUT)
				{
					// Timeout occurred
					make_adjustments();
					continue;
				}
			}
			else
				res = evbuf->WaitForEvent(evbuf);
			if (res == DFB_INTERRUPTED)
				// Shouldn't happen, but does :(
				continue;
			dfbassert(res);
		}
		dfbassert(evbuf->GetEvent(evbuf, &ev));
		if (ev.clazz != DFEC_INPUT)
			continue;
		
		switch (ev.input.type)
		{
			case DIET_AXISMOTION:
				// Knob
				if (!(ev.input.flags & DIEF_AXISREL))
					break;
				
				handle_knob_turn(ev.input.axisrel);
				
				break;
			case DIET_KEYPRESS:
			case DIET_KEYRELEASE:
				if (ev.input.flags & DIEF_KEYID)
				{
					// Simulate knob turn for left/right arrow keys
					if (ev.input.key_id == DIKI_LEFT)
					{
						handle_knob_turn(4);
						break;
					}
					else
					if (ev.input.key_id == DIKI_RIGHT)
					{
						handle_knob_turn(-4);
						break;
					}
				}
				
				handle_button_press();
				
				break;
			default:
				break;
		}
	}
}
