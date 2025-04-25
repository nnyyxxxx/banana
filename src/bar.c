#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"
#include "bar.h"
#include "config.h"

#define BAR_HEIGHT        20
#define MAX_STATUS_LENGTH 256
#define PADDING           4

static char          statusText[MAX_STATUS_LENGTH] = "banana";

Window*              barWindows = NULL;

static unsigned long barBgColor;
static unsigned long barFgColor;
static unsigned long barBorderColor;

static XftFont*      barFont = NULL;
static XftColor      barTextColor;
static XftDraw**     barDraws = NULL;

static Atom          WM_NAME;
static Atom          NET_WM_NAME;

static void          initColors(void) {
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

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_FOREGROUND_COLOR, &barTextColor);
}

static int initFont(void) {
    barFont = XftFontOpenName(display, DefaultScreen(display), BAR_FONT);
    if (!barFont) {
        fprintf(stderr, "Failed to load bar font\n");
        return 0;
    }
    return 1;
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

        WM_NAME     = XInternAtom(display, "WM_NAME", False);
        NET_WM_NAME = XInternAtom(display, "_NET_WM_NAME", False);

        initialized = 1;
    }

    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel  = barBgColor;
    wa.border_pixel      = barBorderColor;
    wa.event_mask        = ExposureMask | ButtonPressMask;

    for (int i = 0; i < numMonitors; i++) {
        barWindows[i] = XCreateWindow(display, root, monitors[i].x, monitors[i].y, monitors[i].width, BAR_HEIGHT, 0, DefaultDepth(display, DefaultScreen(display)), CopyFromParent,
                                      DefaultVisual(display, DefaultScreen(display)), CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &wa);

        barDraws[i] = XftDrawCreate(display, barWindows[i], DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)));

        XMapRaised(display, barWindows[i]);
    }

    updateBars();
}

static char* getWindowTitle(void) {
    static char title[256];

    if (!focused) {
        title[0] = '\0';
        return title;
    }

    XTextProperty textProp;
    if (XGetTextProperty(display, focused->window, &textProp, NET_WM_NAME) || XGetTextProperty(display, focused->window, &textProp, WM_NAME)) {

        if (textProp.encoding == XA_STRING) {
            strncpy(title, (char*)textProp.value, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
        } else {
            strncpy(title, (char*)textProp.value, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
        }

        XFree(textProp.value);
        return title;
    }

    title[0] = '\0';
    return title;
}

static void drawText(int monitorIndex, const char* text, int x, int y, int isCenter) {
    int        textWidth = 0;
    XGlyphInfo extents;

    if (!text || !text[0])
        return;

    XftTextExtentsUtf8(display, barFont, (XftChar8*)text, strlen(text), &extents);
    textWidth = extents.xOff;

    if (isCenter)
        x = x - (textWidth / 2);

    XftDrawStringUtf8(barDraws[monitorIndex], &barTextColor, barFont, x, y + (BAR_HEIGHT + barFont->ascent - barFont->descent) / 2, (XftChar8*)text, strlen(text));
}

void updateStatus(void) {
    XTextProperty textProp;

    if (!XGetTextProperty(display, root, &textProp, XA_WM_NAME) || !textProp.nitems)
        strncpy(statusText, "banana", sizeof(statusText) - 1);
    else {
        strncpy(statusText, (char*)textProp.value, sizeof(statusText) - 1);
        statusText[sizeof(statusText) - 1] = '\0';
        XFree(textProp.value);
    }

    updateBars();
}

void updateBars(void) {
    char* windowTitle = getWindowTitle();

    for (int i = 0; i < numMonitors; i++) {
        if (!barWindows[i] || !barDraws[i])
            continue;

        XClearWindow(display, barWindows[i]);

        XGlyphInfo extents;

        if (statusText[0])
            XftTextExtentsUtf8(display, barFont, (XftChar8*)statusText, strlen(statusText), &extents);

        if (windowTitle[0])
            drawText(i, windowTitle, BAR_PADDING, 0, 0);

        if (statusText[0])
            drawText(i, statusText, monitors[i].width - BAR_PADDING - extents.xOff, 0, 0);
    }

    raiseBars();
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
}

void handleBarExpose(XEvent* event) {
    (void)event;
    updateBars();
}

void updateClientPositionsForBar(void) {
    SClient* client = clients;

    while (client) {
        if (client->y < monitors[client->monitor].y + BAR_HEIGHT) {
            client->y = monitors[client->monitor].y + BAR_HEIGHT;
            XMoveWindow(display, client->window, client->x, client->y);
        }
        client = client->next;
    }
}