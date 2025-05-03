#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bar.h"
#include "config.h"

static char		    *workspaceNames[9];

Window			    *barWindows = NULL;
int			     barVisible = 1;

static unsigned long	     barBgColor;
static unsigned long	     barFgColor;
static unsigned long	     barBorderPixel;
static unsigned long	     barActiveWsPixel;
static unsigned long	     barUrgentWsPixel;

static PangoFontDescription *barFontDesc     = NULL;
static PangoContext	    *barPangoContext = NULL;
static cairo_surface_t	   **barSurfaces     = NULL;
static cairo_t		   **barCairos	     = NULL;
static PangoLayout	   **barLayouts	     = NULL;

static Atom		     WM_NAME;
static Atom		     NET_WM_STRUT;
static Atom		     NET_WM_STRUT_PARTIAL;

static char		     statusText[MAX_STATUS_LENGTH] = "";

static int		     initialized = 0;

static SBarColor	     sBarActiveTextColor;
static SBarColor	     sBarInactiveTextColor;
static SBarColor	     sBarUrgentTextColor;
static SBarColor	     sBarStatusTextColor;

static void xColorToSBarColor(unsigned long pixel, SBarColor *color)
{
	color->r = ((pixel >> 16) & 0xFF) / 255.0;
	color->g = ((pixel >> 8) & 0xFF) / 255.0;
	color->b = ((pixel) & 0xFF) / 255.0;
	color->a = 1.0;
}

static void stringToSBarColor(const char *colorStr, SBarColor *color)
{
	unsigned int r, g, b;
	if (sscanf(colorStr, "#%2x%2x%2x", &r, &g, &b) == 3) {
		color->r = r / 255.0;
		color->g = g / 255.0;
		color->b = b / 255.0;
		color->a = 1.0;
	} else {
		color->r = color->g = color->b = 0.0;
		color->a		       = 1.0;
	}
}

static void initWorkspaceNames(void)
{
	for (int i = 0; i < workspaceCount; i++) {
		workspaceNames[i] = malloc(8);
		if (workspaceNames[i]) {
			snprintf(workspaceNames[i], 8, "%d", i + 1);
		}
	}
}

static void freeWorkspaceNames(void)
{
	for (int i = 0; i < workspaceCount; i++) {
		free(workspaceNames[i]);
		workspaceNames[i] = NULL;
	}
}

static void initColors(void)
{
	XColor	 color;
	Colormap cmap = DefaultColormap(display, DefaultScreen(display));

	if (XAllocNamedColor(display, cmap, barBackgroundColor, &color,
			     &color)) {
		barBgColor = color.pixel;
	} else {
		barBgColor = BlackPixel(display, DefaultScreen(display));
	}

	if (XAllocNamedColor(display, cmap, barForegroundColor, &color,
			     &color)) {
		barFgColor = color.pixel;
	} else {
		barFgColor = WhitePixel(display, DefaultScreen(display));
	}

	if (XAllocNamedColor(display, cmap, barBorderColor, &color, &color)) {
		barBorderPixel = color.pixel;
	} else {
		barBorderPixel = BlackPixel(display, DefaultScreen(display));
	}

	if (XAllocNamedColor(display, cmap, barActiveWsColor, &color, &color)) {
		barActiveWsPixel = color.pixel;
	} else {
		barActiveWsPixel = barFgColor;
	}

	if (XAllocNamedColor(display, cmap, barUrgentWsColor, &color, &color)) {
		barUrgentWsPixel = color.pixel;
	} else {
		barUrgentWsPixel = 0xFF0000;
	}

	stringToSBarColor(barActiveTextColor, &sBarActiveTextColor);
	stringToSBarColor(barUrgentTextColor, &sBarUrgentTextColor);
	stringToSBarColor(barInactiveTextColor, &sBarInactiveTextColor);
	stringToSBarColor(barStatusTextColor, &sBarStatusTextColor);
}

static int initFont(void)
{
	barFontDesc = pango_font_description_from_string(barFont);
	if (!barFontDesc) {
		fprintf(stderr, "Failed to load bar font\n");
		return 0;
	}

	barPangoContext =
	    pango_font_map_create_context(pango_cairo_font_map_get_default());
	if (!barPangoContext) {
		fprintf(stderr, "Failed to create Pango context\n");
		pango_font_description_free(barFontDesc);
		barFontDesc = NULL;
		return 0;
	}

	return 1;
}

static int getTextWidth(const char *text, PangoLayout *layout)
{
	int width, height;
	pango_layout_set_text(layout, text, -1);
	pango_layout_get_pixel_size(layout, &width, &height);
	return width;
}

static void drawText(int monitorIndex, const char *text, int x, int y,
		     SBarColor *color, int isCenter)
{
	if (!text || !text[0]) {
		return;
	}

	cairo_t	    *cr	    = barCairos[monitorIndex];
	PangoLayout *layout = barLayouts[monitorIndex];

	pango_layout_set_text(layout, text, -1);

	int width, height;
	pango_layout_get_pixel_size(layout, &width, &height);

	if (isCenter) {
		x = x - (width / 2);
	}

	cairo_save(cr);
	cairo_set_source_rgba(cr, color->r, color->g, color->b, color->a);
	cairo_move_to(cr, x, y + (barHeight - height) / 2);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);
}

void showHideBars(int show)
{
	if (!barWindows) {
		return;
	}

	barVisible = show;

	for (int i = 0; i < numMonitors; i++) {
		if (barWindows[i]) {
			if (show) {
				XMapWindow(display, barWindows[i]);
			} else {
				XUnmapWindow(display, barWindows[i]);
			}
		}
	}
}

void resetBarResources(void)
{
	if (barLayouts) {
		for (int i = 0; i < numMonitors; i++) {
			if (barLayouts[i]) {
				g_object_unref(barLayouts[i]);
			}
		}
		free(barLayouts);
		barLayouts = NULL;
	}

	if (barCairos) {
		for (int i = 0; i < numMonitors; i++) {
			if (barCairos[i]) {
				cairo_destroy(barCairos[i]);
			}
		}
		free(barCairos);
		barCairos = NULL;
	}

	if (barSurfaces) {
		for (int i = 0; i < numMonitors; i++) {
			if (barSurfaces[i]) {
				cairo_surface_destroy(barSurfaces[i]);
			}
		}
		free(barSurfaces);
		barSurfaces = NULL;
	}

	if (barPangoContext) {
		g_object_unref(barPangoContext);
		barPangoContext = NULL;
	}

	if (barFontDesc) {
		pango_font_description_free(barFontDesc);
		barFontDesc = NULL;
	}

	initialized = 0;
}

void createBars(void)
{
	if (barWindows) {
		for (int i = 0; i < numMonitors; i++) {
			if (barWindows[i] != 0) {
				XDestroyWindow(display, barWindows[i]);

				if (barLayouts && barLayouts[i]) {
					g_object_unref(barLayouts[i]);
				}

				if (barCairos && barCairos[i]) {
					cairo_destroy(barCairos[i]);
				}

				if (barSurfaces && barSurfaces[i]) {
					cairo_surface_destroy(barSurfaces[i]);
				}
			}
		}
		free(barWindows);
		free(barLayouts);
		free(barCairos);
		free(barSurfaces);
	}

	barWindows  = calloc(numMonitors, sizeof(Window));
	barLayouts  = calloc(numMonitors, sizeof(PangoLayout *));
	barCairos   = calloc(numMonitors, sizeof(cairo_t *));
	barSurfaces = calloc(numMonitors, sizeof(cairo_surface_t *));

	if (!barWindows || !barLayouts || !barCairos || !barSurfaces) {
		fprintf(stderr, "Failed to allocate memory for bars\n");
		return;
	}

	if (!initialized) {
		initColors();
		if (!initFont()) {
			free(barWindows);
			barWindows = NULL;
			free(barLayouts);
			barLayouts = NULL;
			free(barCairos);
			barCairos = NULL;
			free(barSurfaces);
			barSurfaces = NULL;
			return;
		}

		WM_NAME	     = XInternAtom(display, "WM_NAME", False);
		NET_WM_STRUT = XInternAtom(display, "_NET_WM_STRUT", False);
		NET_WM_STRUT_PARTIAL =
		    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);

		initWorkspaceNames();

		barVisible  = showBar;
		initialized = 1;
	}

	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.background_pixel  = barBgColor;
	wa.border_pixel	     = barBorderPixel;
	wa.event_mask	     = ExposureMask | ButtonPressMask;

	for (int i = 0; i < numMonitors; i++) {
		int barX = monitors[i].x + barStrutsLeft;
		int barY;
		int barWidth =
		    monitors[i].width - barStrutsLeft - barStrutsRight;

		if (bottomBar) {
			barY = monitors[i].y + monitors[i].height - barHeight -
			       barStrutsTop;
		} else {
			barY = monitors[i].y + barStrutsTop;
		}

		barWindows[i] = XCreateWindow(
		    display, root, barX, barY, barWidth, barHeight,
		    barBorderWidth,
		    DefaultDepth(display, DefaultScreen(display)),
		    CopyFromParent,
		    DefaultVisual(display, DefaultScreen(display)),
		    CWOverrideRedirect | CWBackPixel | CWBorderPixel |
			CWEventMask,
		    &wa);

		barSurfaces[i] = cairo_xlib_surface_create(
		    display, barWindows[i],
		    DefaultVisual(display, DefaultScreen(display)), barWidth,
		    barHeight);
		barCairos[i] = cairo_create(barSurfaces[i]);

		barLayouts[i] = pango_cairo_create_layout(barCairos[i]);
		pango_layout_set_font_description(barLayouts[i], barFontDesc);

		long struts[12] = {0};

		if (bottomBar) {
			struts[3] = barHeight + barBorderWidth * 2;

			struts[10] = monitors[i].x;
			struts[11] = monitors[i].x + monitors[i].width - 1;
		} else {
			struts[2] = barY + barHeight + barBorderWidth * 2;

			struts[4] = monitors[i].x;
			struts[5] = monitors[i].x + monitors[i].width - 1;
		}

		XChangeProperty(display, barWindows[i], NET_WM_STRUT,
				XA_CARDINAL, 32, PropModeReplace,
				(unsigned char *)&struts, 4);
		XChangeProperty(display, barWindows[i], NET_WM_STRUT_PARTIAL,
				XA_CARDINAL, 32, PropModeReplace,
				(unsigned char *)&struts, 12);

		if (barVisible) {
			XMapWindow(display, barWindows[i]);
		}
	}

	updateBars();
}

void updateStatus(void)
{
	XTextProperty textProp;
	if (XGetTextProperty(display, root, &textProp, XA_WM_NAME)) {
		if (textProp.encoding == XA_STRING) {
			strncpy(statusText, (char *)textProp.value,
				sizeof(statusText) - 1);
			statusText[sizeof(statusText) - 1] = '\0';
		} else {
			strncpy(statusText, (char *)textProp.value,
				sizeof(statusText) - 1);
			statusText[sizeof(statusText) - 1] = '\0';
		}

		XFree(textProp.value);
	}

	updateBars();
}

static int workspaceHasUrgentWindow(int monitor, int workspace)
{
	SClient *client = clients;
	while (client) {
		if (client->monitor == monitor &&
		    client->workspace == workspace && client->isUrgent) {
			return 1;
		}
		client = client->next;
	}
	return 0;
}

static int workspaceHasClients(int monitor, int workspace)
{
	SClient *client = clients;
	while (client) {
		if (client->monitor == monitor &&
		    client->workspace == workspace) {
			return 1;
		}
		client = client->next;
	}
	return 0;
}

void updateBars(void)
{
	if (!barWindows || !barVisible) {
		return;
	}

	for (int i = 0; i < numMonitors; i++) {
		if (!barWindows[i] || !barCairos[i] || !barLayouts[i]) {
			continue;
		}

		cairo_t	 *cr = barCairos[i];
		SBarColor bgColor;
		xColorToSBarColor(barBgColor, &bgColor);

		cairo_set_source_rgba(cr, bgColor.r, bgColor.g, bgColor.b,
				      bgColor.a);
		cairo_paint(cr);

		int x = 0;

		int maxTextWidth = 0;
		for (int w = 0; w < workspaceCount; w++) {
			int textWidth =
			    getTextWidth(workspaceNames[w], barLayouts[i]);
			if (textWidth > maxTextWidth) {
				maxTextWidth = textWidth;
			}
		}

		int wsWidth = maxTextWidth + 16;

		for (int w = 0; w < workspaceCount; w++) {
			if (showOnlyActiveWorkspaces &&
			    monitors[i].currentWorkspace != w &&
			    !workspaceHasClients(i, w) &&
			    !workspaceHasUrgentWindow(i, w)) {
				continue;
			}

			SBarColor *textColor = &sBarInactiveTextColor;
			SBarColor  wsBgColor;
			int	   hasUrgent = workspaceHasUrgentWindow(i, w);

			if (monitors[i].currentWorkspace == w) {
				textColor = &sBarActiveTextColor;
				xColorToSBarColor(barActiveWsPixel, &wsBgColor);
			} else if (hasUrgent) {
				textColor = &sBarUrgentTextColor;
				xColorToSBarColor(barUrgentWsPixel, &wsBgColor);
			} else {
				xColorToSBarColor(barBgColor, &wsBgColor);
			}

			cairo_set_source_rgba(cr, wsBgColor.r, wsBgColor.g,
					      wsBgColor.b, wsBgColor.a);
			cairo_rectangle(cr, x, 0, wsWidth, barHeight);
			cairo_fill(cr);

			char wsLabel[32];
			snprintf(wsLabel, sizeof(wsLabel), " %s ",
				 workspaceNames[w]);

			int textX = x + (wsWidth -
					 getTextWidth(wsLabel, barLayouts[i])) /
					    2;
			drawText(i, wsLabel, textX, 0, textColor, 0);

			x += wsWidth;
		}

		int barWidth =
		    monitors[i].width - barStrutsLeft - barStrutsRight;

		if (statusText[0] != '\0') {
			drawText(i, statusText,
				 barWidth -
				     getTextWidth(statusText, barLayouts[i]),
				 0, &sBarStatusTextColor, 0);
		}

		cairo_surface_flush(barSurfaces[i]);
	}
}

void raiseBars(void)
{
	if (!barWindows) {
		return;
	}

	for (int i = 0; i < numMonitors; i++) {
		if (barWindows[i]) {
			XRaiseWindow(display, barWindows[i]);
		}
	}
}

void cleanupBars(void)
{
	if (barWindows) {
		for (int i = 0; i < numMonitors; i++) {
			if (barWindows[i]) {
				XDestroyWindow(display, barWindows[i]);

				if (barLayouts && barLayouts[i]) {
					g_object_unref(barLayouts[i]);
				}

				if (barCairos && barCairos[i]) {
					cairo_destroy(barCairos[i]);
				}

				if (barSurfaces && barSurfaces[i]) {
					cairo_surface_destroy(barSurfaces[i]);
				}
			}
		}
		free(barWindows);
		barWindows = NULL;
		free(barLayouts);
		barLayouts = NULL;
		free(barCairos);
		barCairos = NULL;
		free(barSurfaces);
		barSurfaces = NULL;
	}

	freeWorkspaceNames();
}

void handleBarExpose(XEvent *event)
{
	(void)event;
	updateBars();
}

void handleBarClick(XEvent *event)
{
	XButtonEvent *ev = &event->xbutton;

	if (ev->button != Button1) {
		return;
	}

	for (int i = 0; i < numMonitors; i++) {
		if (ev->window != barWindows[i]) {
			continue;
		}

		int maxTextWidth = 0;
		for (int w = 0; w < workspaceCount; w++) {
			int textWidth =
			    getTextWidth(workspaceNames[w], barLayouts[i]);
			if (textWidth > maxTextWidth) {
				maxTextWidth = textWidth;
			}
		}

		int wsWidth = maxTextWidth + 16;

		if (showOnlyActiveWorkspaces) {
			int clickPos = 0;
			for (int w = 0; w < workspaceCount; w++) {
				if (monitors[i].currentWorkspace != w &&
				    !workspaceHasClients(i, w) &&
				    !workspaceHasUrgentWindow(i, w)) {
					continue;
				}

				if (ev->x >= clickPos &&
				    ev->x < clickPos + wsWidth) {
					monitors[i].currentWorkspace = w;
					updateClientVisibility();
					updateBars();
					break;
				}
				clickPos += wsWidth;
			}
		} else {
			int x = 0;
			for (int w = 0; w < workspaceCount; w++) {
				if (ev->x >= x && ev->x < x + wsWidth) {
					monitors[i].currentWorkspace = w;
					updateClientVisibility();
					updateBars();
					break;
				}
				x += wsWidth;
			}
		}
		break;
	}
}

void updateClientPositionsForBar(void)
{
	SClient *client = clients;

	while (client) {
		if (client->isFloating || client->isFullscreen) {
			client = client->next;
			continue;
		}

		SMonitor *m = &monitors[client->monitor];

		if (!barVisible) {
			if (!bottomBar &&
			    client->y == m->y + barStrutsTop + barHeight +
					     barBorderWidth * 2 + outerGap) {
				client->y = m->y + outerGap;
				XMoveWindow(display, client->window, client->x,
					    client->y);
			}
			client = client->next;
			continue;
		}

		if (bottomBar) {
			int barTop = m->y + m->height - barStrutsTop -
				     barHeight - barBorderWidth * 2;

			if (client->y + client->height > barTop - outerGap) {
				int newHeight = barTop - client->y - outerGap;
				if (newHeight > 0) {
					client->height = newHeight;
					XResizeWindow(display, client->window,
						      client->width,
						      client->height);
				}
			}
		} else {
			int barBottom = m->y + barStrutsTop + barHeight +
					barBorderWidth * 2;

			if (client->y < barBottom + outerGap) {
				client->y = barBottom + outerGap;
				XMoveWindow(display, client->window, client->x,
					    client->y);
			}
		}

		client = client->next;
	}
}