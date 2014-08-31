#ifndef FABD_WALLKNOB_H
#define FABD_WALLKNOB_H

#include <directfb.h>

extern void dfbassert_(DFBResult, const char *, int line, const char *);
#define dfbassert(expr)  dfbassert_(expr, __FILE__, __LINE__, #expr)

typedef void (*wallknob_event_handler_func)(DFBEvent *);
extern wallknob_event_handler_func current_event_handler;

static inline
wallknob_event_handler_func ts_current_event_handler(const wallknob_event_handler_func n)
{
	wallknob_event_handler_func o = current_event_handler;
	current_event_handler = n;
	return o;
}

struct my_window_info {
	IDirectFBWindow *win;
	IDirectFBSurface *surface;
	DFBDimension sz;
};

extern void my_win_init(struct my_window_info *);

struct my_font {
	IDirectFBFont *dfbfont;
	int height;
	int ascender;
	int descender;
	int width_x;
};
extern struct my_font font_h2, font_h4;

extern void fabdwk_wait_for_event(DFBEvent *);

#endif
