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

static char*         workspaceNames[WORKSPACE_COUNT];

Window*              barWindows = NULL;
int                  barVisible = SHOW_BAR;

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
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        workspaceNames[i] = malloc(8);
        if (workspaceNames[i])
            snprintf(workspaceNames[i], 8, "%d", i + 1);
    }
}

static void freeWorkspaceNames(void) {
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        free(workspaceNames[i]);
        workspaceNames[i] = NULL;
    }
}

static void initColors(void) {
    XColor   color;
    Colormap cmap = DefaultColormap(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, BAR_BACKGROUND_COLOR, &color, &color))
        barBgColor = color.pixel;
    else
        barBgColor = BlackPixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, BAR_FOREGROUND_COLOR, &color, &color))
        barFgColor = color.pixel;
    else
        barFgColor = WhitePixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, BAR_BORDER_COLOR, &color, &color))
        barBorderColor = color.pixel;
    else
        barBorderColor = BlackPixel(display, DefaultScreen(display));

    if (XAllocNamedColor(display, cmap, BAR_ACTIVE_WS_COLOR, &color, &color))
        barActiveWsColor = color.pixel;
    else
        barActiveWsColor = barFgColor;

    if (XAllocNamedColor(display, cmap, BAR_URGENT_WS_COLOR, &color, &color))
        barUrgentWsColor = color.pixel;
    else
        barUrgentWsColor = 0xFF0000;

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_ACTIVE_TEXT_COLOR, &barActiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_URGENT_TEXT_COLOR, &barUrgentTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_INACTIVE_TEXT_COLOR, &barInactiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_STATUS_TEXT_COLOR, &barStatusTextColor);
}

static int initFont(void) {
    barFont = XftFontOpenName(display, DefaultScreen(display), BAR_FONT);
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

    XftDrawStringUtf8(barDraws[monitorIndex], color, barFont, x, y + (BAR_HEIGHT + barFont->ascent - barFont->descent) / 2, (XftChar8*)text, strlen(text));
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

        initialized = 1;
    }

    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel  = barBgColor;
    wa.border_pixel      = barBorderColor;
    wa.event_mask        = ExposureMask | ButtonPressMask;

    for (int i = 0; i < numMonitors; i++) {
        int barX     = monitors[i].x + BAR_STRUTS_LEFT;
        int barY     = monitors[i].y + BAR_STRUTS_TOP;
        int barWidth = monitors[i].width - BAR_STRUTS_LEFT - BAR_STRUTS_RIGHT;

        barWindows[i] = XCreateWindow(display, root, barX, barY, barWidth, BAR_HEIGHT, BAR_BORDER_WIDTH, DefaultDepth(display, DefaultScreen(display)), CopyFromParent,
                                      DefaultVisual(display, DefaultScreen(display)), CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &wa);

        barDraws[i] = XftDrawCreate(display, barWindows[i], DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)));

        long struts[12] = {0};

        struts[2] = barY + BAR_HEIGHT + BAR_BORDER_WIDTH * 2;

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
        for (int w = 0; w < WORKSPACE_COUNT; w++) {
            int textWidth = getTextWidth(workspaceNames[w]);
            if (textWidth > maxTextWidth)
                maxTextWidth = textWidth;
        }

        int wsWidth = maxTextWidth + 16;

        for (int w = 0; w < WORKSPACE_COUNT; w++) {
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
            XFillRectangle(display, barWindows[i], DefaultGC(display, DefaultScreen(display)), x, 0, wsWidth, BAR_HEIGHT);

            char wsLabel[32];
            snprintf(wsLabel, sizeof(wsLabel), " %s ", workspaceNames[w]);

            int textX = x + (wsWidth - getTextWidth(wsLabel)) / 2;
            drawText(i, wsLabel, textX, 0, textColor, 0);

            x += wsWidth;
        }

        int barWidth = monitors[i].width - BAR_STRUTS_LEFT - BAR_STRUTS_RIGHT;

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
            for (int w = 0; w < WORKSPACE_COUNT; w++) {
                int textWidth = getTextWidth(workspaceNames[w]);
                if (textWidth > maxTextWidth)
                    maxTextWidth = textWidth;
            }

            int wsWidth = maxTextWidth + 16;

            for (int w = 0; w < WORKSPACE_COUNT; w++) {
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
                int barBottom = m->y + BAR_STRUTS_TOP + BAR_HEIGHT + BAR_BORDER_WIDTH * 2;
                if (client->y < barBottom + OUTER_GAP) {
                    client->y = barBottom + OUTER_GAP;
                    XMoveWindow(display, client->window, client->x, client->y);
                }
            } else if (!barVisible && client->y == m->y + BAR_STRUTS_TOP + BAR_HEIGHT + BAR_BORDER_WIDTH * 2 + OUTER_GAP) {
                client->y = m->y + OUTER_GAP;
                XMoveWindow(display, client->window, client->x, client->y);
            }
        }
        client = client->next;
    }
}