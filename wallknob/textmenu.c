#include "config.h"

#include <directfb.h>

#include <freeabode/util.h>

#include "wallknob.h"

static
void fabdwk_textmenu_draw(struct my_window_info * const wi, struct my_font * const font, const char * const prompt, const char * const * const opts, const int optcount, const double scroll, int * const out_sel)
{
	dfbassert(wi->surface->Clear(wi->surface, 0xff, 0xff, 0xff, 0xff));
	dfbassert(wi->surface->SetColor(wi->surface, 0, 0, 0, 0xff));
	dfbassert(wi->surface->SetFont(wi->surface, font->dfbfont));
	int linewidth, linechars;
	const int center_x = wi->sz.w / 2;
	const int center_y = wi->sz.h / 2;
	int y = -scroll * font->height;
	const char *nextline;
	for (const char *line = prompt; line && line[0]; line = nextline)
	{
		dfbassert(font->dfbfont->GetStringBreak(font->dfbfont, line, -1, wi->sz.w, &linewidth, &linechars, &nextline));
		y += font->height;
		dfbassert(wi->surface->DrawString(wi->surface, line, linechars, center_x, y, DSTF_CENTER));
	}
	y += font->height;
	int first_opt_y1 = y;
	for (int i = 0; i < optcount; ++i)
		dfbassert(wi->surface->DrawString(wi->surface, opts[i], -1, center_x, (y += font->height), DSTF_CENTER));
	
	int best_opt = (center_y - first_opt_y1) / font->height;
	best_opt = fabd_min(optcount - 1, fabd_max(0, best_opt));
	
	y = first_opt_y1 + (font->height * best_opt);
	dfbassert(font->dfbfont->GetStringWidth(font->dfbfont, opts[best_opt], -1, &linewidth));
	{
		int bgwidth = linewidth + font->width_x;
		DFBRectangle rect = {
			.x = center_x - (bgwidth / 2),
			.y = y,
			.w = bgwidth,
			.h = font->height - font->descender,
		};
		dfbassert(wi->surface->SetColor(wi->surface, 0, 0x7f, 0xff, 0xff));
		dfbassert(wi->surface->FillRectangles(wi->surface, &rect, 1));
	}
	dfbassert(wi->surface->SetColor(wi->surface, 0xff, 0xff, 0xff, 0xff));
	dfbassert(wi->surface->DrawString(wi->surface, opts[best_opt], -1, center_x, y + font->height, DSTF_CENTER));
	*out_sel = best_opt;
	
	dfbassert(wi->win->SetOpacity(wi->win, 0xff));
	dfbassert(wi->surface->Flip(wi->surface, NULL, DSFLIP_BLIT));
}

int fabdwk_textmenu(struct my_window_info * const wi, const char * const prompt, const char * const * const opts, const int optcount, const int defopt)
{
	struct my_font * const font = &font_h4;
	double scroll = 1;
	int sel;
	
	{
		int linewidth, linechars;
		const char *nextline;
		for (const char *line = prompt; line && line[0]; line = nextline)
		{
			dfbassert(font->dfbfont->GetStringBreak(font->dfbfont, line, -1, wi->sz.w, &linewidth, &linechars, &nextline));
			++scroll;
		}
		const int center_y = wi->sz.h / 2;
		scroll -= center_y / font->height;
		scroll += defopt;
	}
	
	DFBEvent ev;
redraw: ;
	fabdwk_textmenu_draw(wi, font, prompt, opts, optcount, scroll, &sel);
	while (true)
	{
		fabdwk_wait_for_event(&ev);
		
		if (ev.clazz != DFEC_INPUT)
			continue;
		
		switch (ev.input.type)
		{
			case DIET_AXISMOTION:
				if (!(ev.input.flags & DIEF_AXISREL))
					break;
				
				scroll -= (double)ev.input.axisrel / 0x100;
				goto redraw;
			case DIET_KEYPRESS:
				switch ((ev.input.flags & DIEF_KEYID) ? ev.input.key_id : DIKI_UNKNOWN)
				{
					case DIKI_UP:
						--scroll;
						goto redraw;
					case DIKI_DOWN:
						++scroll;
						goto redraw;
					default:
						goto done;
				}
			default:
				break;
		}
	}
done: ;
	dfbassert(wi->win->SetOpacity(wi->win, 0));
	return sel;
}
