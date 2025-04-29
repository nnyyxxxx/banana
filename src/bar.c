#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bar.h"
#include "config.h"

#define MAX_STATUS_LENGTH 256

static char*         workspaceNames[9];

Window*              barWindows = NULL;
int                  barVisible = 1;

static unsigned long barBgColor;
static unsigned long barFgColor;
static unsigned long barBorderColor;
static unsigned long barActiveWsColor;
static unsigned long barUrgentWsColor;

static XftFont*      barFont = NULL;
static XftColor      barActiveTextColor;
static XftColor      barInactiveTextColor;
static XftColor      barUrgentTextColor;
static XftColor      barStatusTextColor;
static XftDraw**     barDraws = NULL;

static Atom          WM_NAME;
static Atom          NET_WM_STRUT;
static Atom          NET_WM_STRUT_PARTIAL;

static char          statusText[MAX_STATUS_LENGTH] = "";

static void          initWorkspaceNames(void) {
    for (int i = 0; i < workspace_count; i++) {
        workspaceNames[i] = malloc(8);
        if (workspaceNames[i])
            snprintf(workspaceNames[i], 8, "%d", i + 1);
    }
}

static void freeWorkspaceNames(void) {
    for (int i = 0; i < workspace_count; i++) {
        free(workspaceNames[i]);
        workspaceNames[i] = NULL;
    }
}

static void initColors(void) {
    XColor   color;
    Colormap cmap = DefaultColormap(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, bar_background_color, &color, &color))
        barBgColor = color.pixel;
    else
        barBgColor = BlackPixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, bar_foreground_color, &color, &color))
        barFgColor = color.pixel;
    else
        barFgColor = WhitePixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, bar_border_color, &color, &color))
        barBorderColor = color.pixel;
    else
        barBorderColor = BlackPixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, bar_active_ws_color, &color, &color))
        barActiveWsColor = color.pixel;
    else
        barActiveWsColor = barFgColor;

    if (XAllocNamedColor(display, cmap, bar_urgent_ws_color, &color, &color))
        barUrgentWsColor = color.pixel;
    else
        barUrgentWsColor = 0xFF0000;

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), bar_active_text_color, &barActiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), bar_urgent_text_color, &barUrgentTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), bar_inactive_text_color, &barInactiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), bar_status_text_color, &barStatusTextColor);
}

static int initFont(void) {
    barFont = XftFontOpenName(display, DefaultScreen(display), bar_font);
    if (!barFont) {
        fprintf(stderr, "Failed to load bar font\n");
        return 0;
    }
    return 1;
}

static int getTextWidth(const char* text) {
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, barFont, (XftChar8*)text, strlen(text), &extents);
    return extents.xOff;
}

static void drawText(int monitorIndex, const char* text, int x, int y, XftColor* color, int isCenter) {
    if (!text || !text[0])
        return;

    int txtWidth = getTextWidth(text);

    if (isCenter)
        x = x - (txtWidth / 2);

    XftDrawStringUtf8(barDraws[monitorIndex], color, barFont, x, y + (bar_height + barFont->ascent - barFont->descent) / 2, (XftChar8*)text, strlen(text));
}

void showHideBars(int show) {
    if (!barWindows)
        return;

    barVisible = show;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows[i]) {
            if (show)
                XMapWindow(display, barWindows[i]);
            else
                XUnmapWindow(display, barWindows[i]);
        }
    }
}

void createBars(void) {
    if (barWindows) {
        for (int i = 0; i < numMonitors; i++) {
            if (barWindows[i] != 0) {
                XDestroyWindow(display, barWindows[i]);
                if (barDraws && barDraws[i])
                    XftDrawDestroy(barDraws[i]);
            }
        }
        free(barWindows);
        free(barDraws);
    }

    barWindows = calloc(numMonitors, sizeof(Window));
    barDraws   = calloc(numMonitors, sizeof(XftDraw*));

    if (!barWindows || !barDraws) {
        fprintf(stderr, "Failed to allocate memory for bars\n");
        return;
    }

    static int initialized = 0;
    if (!initialized) {
        initColors();
        if (!initFont()) {
            free(barWindows);
            barWindows = NULL;
            free(barDraws);
            barDraws = NULL;
            return;
        }

        WM_NAME              = XInternAtom(display, "WM_NAME", False);
        NET_WM_STRUT         = XInternAtom(display, "_NET_WM_STRUT", False);
        NET_WM_STRUT_PARTIAL = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);

        initWorkspaceNames();

        barVisible = show_bar;
        initialized = 1;
    }

    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel  = barBgColor;
    wa.border_pixel      = barBorderColor;
    wa.event_mask        = ExposureMask | ButtonPressMask;

    for (int i = 0; i < numMonitors; i++) {
        int barX     = monitors[i].x + bar_struts_left;
        int barY     = monitors[i].y + bar_struts_top;
        int barWidth = monitors[i].width - bar_struts_left - bar_struts_right;

        barWindows[i] = XCreateWindow(display, root, barX, barY, barWidth, bar_height, bar_border_width, DefaultDepth(display, DefaultScreen(display)), CopyFromParent,
                                      DefaultVisual(display, DefaultScreen(display)), CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &wa);

        barDraws[i] = XftDrawCreate(display, barWindows[i], DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)));

        long struts[12] = {0};

        struts[2] = barY + bar_height + bar_border_width * 2;

        struts[4] = monitors[i].x;
        struts[5] = monitors[i].x + monitors[i].width - 1;

        XChangeProperty(display, barWindows[i], NET_WM_STRUT, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&struts, 4);

        XChangeProperty(display, barWindows[i], NET_WM_STRUT_PARTIAL, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&struts, 12);

        if (barVisible)
            XMapWindow(display, barWindows[i]);
    }

    updateBars();
}

void updateStatus(void) {
    XTextProperty textProp;
    if (XGetTextProperty(display, root, &textProp, XA_WM_NAME)) {
        if (textProp.encoding == XA_STRING) {
            strncpy(statusText, (char*)textProp.value, sizeof(statusText) - 1);
            statusText[sizeof(statusText) - 1] = '\0';
        } else {
            strncpy(statusText, (char*)textProp.value, sizeof(statusText) - 1);
            statusText[sizeof(statusText) - 1] = '\0';
        }

        XFree(textProp.value);
    }

    updateBars();
}

static int workspaceHasUrgentWindow(int monitor, int workspace) {
    SClient* client = clients;
    while (client) {
        if (client->monitor == monitor && client->workspace == workspace && client->isUrgent)
            return 1;
        client = client->next;
    }
    return 0;
}

void updateBars(void) {
    if (!barWindows || !barVisible)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (!barWindows[i] || !barDraws[i])
            continue;

        XClearWindow(display, barWindows[i]);

        int x = 0;

        int maxTextWidth = 0;
        for (int w = 0; w < workspace_count; w++) {
            int textWidth = getTextWidth(workspaceNames[w]);
            if (textWidth > maxTextWidth)
                maxTextWidth = textWidth;
        }

        int wsWidth = maxTextWidth + 16;

        for (int w = 0; w < workspace_count; w++) {
            XftColor* textColor = &barInactiveTextColor;
            int       wsBgColor = barBgColor;
            int       hasUrgent = workspaceHasUrgentWindow(i, w);

            if (monitors[i].currentWorkspace == w) {
                textColor = &barActiveTextColor;
                wsBgColor = barActiveWsColor;
            } else if (hasUrgent) {
                textColor = &barUrgentTextColor;
                wsBgColor = barUrgentWsColor;
            }

            XSetForeground(display, DefaultGC(display, DefaultScreen(display)), wsBgColor);
            XFillRectangle(display, barWindows[i], DefaultGC(display, DefaultScreen(display)), x, 0, wsWidth, bar_height);

            char wsLabel[32];
            snprintf(wsLabel, sizeof(wsLabel), " %s ", workspaceNames[w]);

            int textX = x + (wsWidth - getTextWidth(wsLabel)) / 2;
            drawText(i, wsLabel, textX, 0, textColor, 0);

            x += wsWidth;
        }

        int barWidth = monitors[i].width - bar_struts_left - bar_struts_right;

        if (statusText[0] != '\0')
            drawText(i, statusText, barWidth - getTextWidth(statusText), 0, &barStatusTextColor, 0);
    }
}

void raiseBars(void) {
    if (!barWindows)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows[i])
            XRaiseWindow(display, barWindows[i]);
    }
}

void cleanupBars(void) {
    if (barWindows) {
        for (int i = 0; i < numMonitors; i++) {
            if (barWindows[i]) {
                XDestroyWindow(display, barWindows[i]);
                if (barDraws && barDraws[i])
                    XftDrawDestroy(barDraws[i]);
            }
        }
        free(barWindows);
        barWindows = NULL;
        free(barDraws);
        barDraws = NULL;
    }

    if (barFont) {
        XftFontClose(display, barFont);
        barFont = NULL;
    }

    freeWorkspaceNames();
}

void handleBarExpose(XEvent* event) {
    (void)event;
    updateBars();
}

void handleBarClick(XEvent* event) {
    XButtonEvent* ev = &event->xbutton;

    if (ev->button != Button1)
        return;

    for (int i = 0; i < numMonitors; i++) {
        if (ev->window == barWindows[i]) {
            int x = 0;

            int maxTextWidth = 0;
            for (int w = 0; w < workspace_count; w++) {
                int textWidth = getTextWidth(workspaceNames[w]);
                if (textWidth > maxTextWidth)
                    maxTextWidth = textWidth;
            }

            int wsWidth = maxTextWidth + 16;

            for (int w = 0; w < workspace_count; w++) {
                if (ev->x >= x && ev->x < x + wsWidth) {
                    monitors[i].currentWorkspace = w;
                    updateClientVisibility();
                    updateBars();
                    break;
                }
                x += wsWidth;
            }

            break;
        }
    }
}

void updateClientPositionsForBar(void) {
    SClient* client = clients;

    while (client) {
        if (!client->isFloating && !client->isFullscreen) {
            SMonitor* m = &monitors[client->monitor];
            if (barVisible) {
                int barBottom = m->y + bar_struts_top + bar_height + bar_border_width * 2;
                if (client->y < barBottom + outer_gap) {
                    client->y = barBottom + outer_gap;
                    XMoveWindow(display, client->window, client->x, client->y);
                }
            } else if (!barVisible && client->y == m->y + bar_struts_top + bar_height + bar_border_width * 2 + outer_gap) {
                client->y = m->y + outer_gap;
                XMoveWindow(display, client->window, client->x, client->y);
            }
        }
        client = client->next;
    }
}