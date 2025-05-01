#ifndef BAR_H
#define BAR_H

#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include "banana.h"

#define MAX_STATUS_LENGTH 256

typedef struct {
    cairo_surface_t*      surface;
    cairo_t*              cr;
    PangoLayout*          layout;
    PangoFontDescription* font_desc;
} bar_draw_context_t;

void                       toggleBar(const char* arg);

extern xcb_window_t*       barWindows;
extern int                 barVisible;
extern bar_draw_context_t* bar_contexts;

void                       createBars(void);
void                       updateStatus(void);
void                       updateBars(void);
void                       raiseBars(void);
void                       handleBarExpose(xcb_generic_event_t* event);
void                       handleBarClick(xcb_generic_event_t* event);
void                       cleanupBars(void);
void                       updateClientPositionsForBar(void);
void                       showHideBars(int show);
void                       resetBarResources(void);

#endif /* BAR_H */