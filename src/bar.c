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

static char*         workspaceNames[WORKSPACE_COUNT];

Window*              barWindows = NULL;

static unsigned long barBgColor;
static unsigned long barFgColor;
static unsigned long barBorderColor;
static unsigned long barActiveWsColor;
static unsigned long barUrgentWsColor;
static unsigned long barTitleBgColor;

static XftFont*      barFont = NULL;
static XftColor      barActiveTextColor;
static XftColor      barInactiveTextColor;
static XftColor      barUrgentTextColor;
static XftColor      barStatusTextColor;
static XftColor      barTitleTextColor;
static XftDraw**     barDraws = NULL;

static Atom          WM_NAME;

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

    if (XAllocNamedColor(display, cmap, BAR_TITLE_BG_COLOR, &color, &color))
        barTitleBgColor = color.pixel;
    else
        barTitleBgColor = barBgColor;

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_ACTIVE_TEXT_COLOR, &barActiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_URGENT_TEXT_COLOR, &barUrgentTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_INACTIVE_TEXT_COLOR, &barInactiveTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_STATUS_TEXT_COLOR, &barStatusTextColor);

    XftColorAllocName(display, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)), BAR_TITLE_TEXT_COLOR, &barTitleTextColor);
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

        WM_NAME = XInternAtom(display, "WM_NAME", False);

        initWorkspaceNames();

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

        XMapWindow(display, barWindows[i]);
    }

    if (ENABLE_SYSTRAY)
        createSystray();

    updateBars();
}

char* getWindowTitle(SClient* client) {
    static char title[256];

    if (!client) {
        title[0] = '\0';
        return title;
    }

    XTextProperty textProp;
    if (XGetTextProperty(display, client->window, &textProp, NET_WM_NAME) || XGetTextProperty(display, client->window, &textProp, WM_NAME)) {
        if (textProp.encoding == XA_STRING) {
            strncpy(title, (char*)textProp.value, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
        } else {
            strncpy(title, (char*)textProp.value, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
        }

        XFree(textProp.value);

        if (strlen(title) > MAX_TITLE_LENGTH) {
            int truncPos = MAX_TITLE_LENGTH - 3;

            while (truncPos > 0 && title[truncPos - 1] == ' ')
                truncPos--;

            strcpy(&title[truncPos], "...");
        }

        return title;
    }

    title[0] = '\0';
    return title;
}

static void drawText(int monitorIndex, const char* text, int x, int y, XftColor* color, int isCenter) {
    if (!text || !text[0])
        return;

    int txtWidth = getTextWidth(text);

    if (isCenter)
        x = x - (txtWidth / 2);

    XftDrawStringUtf8(barDraws[monitorIndex], color, barFont, x, y + (BAR_HEIGHT + barFont->ascent - barFont->descent) / 2, (XftChar8*)text, strlen(text));
}

static SClient* getMonitorFocusedClient(int monitor) {
    if (focused && focused->monitor == monitor)
        return focused;

    return NULL;
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
    SMonitor* activeMonitor = getCurrentMonitor();

    for (int i = 0; i < numMonitors; i++) {
        if (!barWindows[i] || !barDraws[i])
            continue;

        XClearWindow(display, barWindows[i]);

        int x = PADDING;

        int maxTextWidth = 0;
        for (int w = 0; w < WORKSPACE_COUNT; w++) {
            int textWidth = getTextWidth(workspaceNames[w]);
            if (textWidth > maxTextWidth)
                maxTextWidth = textWidth;
        }

        int wsWidth = maxTextWidth + (PADDING * 4);

        for (int w = 0; w < WORKSPACE_COUNT; w++) {
            int isSelected = (monitors[i].currentWorkspace == w);
            int isUrgent   = workspaceHasUrgentWindow(i, w);

            if (isSelected) {
                XSetForeground(display, DefaultGC(display, DefaultScreen(display)), barActiveWsColor);
                XFillRectangle(display, barWindows[i], DefaultGC(display, DefaultScreen(display)), x, 0, wsWidth, BAR_HEIGHT);
            } else if (isUrgent) {
                XSetForeground(display, DefaultGC(display, DefaultScreen(display)), barUrgentWsColor);
                XFillRectangle(display, barWindows[i], DefaultGC(display, DefaultScreen(display)), x, 0, wsWidth, BAR_HEIGHT);
            }

            int       centerX   = x + (wsWidth / 2);
            XftColor* textColor = &barInactiveTextColor;
            if (isSelected)
                textColor = &barActiveTextColor;
            else if (isUrgent)
                textColor = &barUrgentTextColor;

            drawText(i, workspaceNames[w], centerX, 0, textColor, 1);

            x += wsWidth;
        }

        SClient* monFocused = getMonitorFocusedClient(i);

        int      statusWidth  = 0;
        int      systrayWidth = 0;

        if (i == activeMonitor->num) {
            if (statusText[0] != '\0')
                statusWidth = getTextWidth(statusText) + PADDING * 2;

            if (ENABLE_SYSTRAY) {
                systrayWidth = getSystrayWidth();
                if (systrayWidth > 0)
                    systrayWidth += PADDING;
            }
        }

        int titleBackgroundWidth = monitors[i].width - x - statusWidth - systrayWidth;

        if (monFocused) {
            char* windowTitle = getWindowTitle(monFocused);
            if (windowTitle && windowTitle[0] != '\0') {
                XSetForeground(display, DefaultGC(display, DefaultScreen(display)), barTitleBgColor);
                XFillRectangle(display, barWindows[i], DefaultGC(display, DefaultScreen(display)), x, 0, titleBackgroundWidth, BAR_HEIGHT);

                drawText(i, windowTitle, x + PADDING, 0, &barTitleTextColor, 0);
            }
        }

        if (ENABLE_SYSTRAY && systrayWidth > 0 && i == activeMonitor->num) {
            int           systrayX = monitors[i].width - statusWidth - systrayWidth + PADDING;

            SSystrayIcon* icon;
            int           iconX = systrayX;

            for (icon = systrayIcons; icon; icon = icon->next) {
                XWindowAttributes wa;
                if (!XGetWindowAttributes(display, icon->win, &wa))
                    continue;

                XMoveResizeWindow(display, icon->win, iconX, (BAR_HEIGHT - systrayIconSize) / 2, systrayIconSize, systrayIconSize);
                XMapWindow(display, icon->win);
                iconX += systrayIconSize + systraySpacing;
            }
        }

        if (statusText[0] != '\0' && i == activeMonitor->num)
            drawText(i, statusText, monitors[i].width - PADDING - getTextWidth(statusText), 0, &barStatusTextColor, 0);
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

    if (ENABLE_SYSTRAY)
        cleanupSystray();

    freeWorkspaceNames();
}

void handleBarExpose(XEvent* event) {
    (void)event;
    updateBars();
}

void handleBarClick(XEvent* event) {
    XButtonEvent* ev = &event->xbutton;

    for (int i = 0; i < numMonitors; i++) {
        if (ev->window == barWindows[i]) {
            int x = PADDING;

            int maxTextWidth = 0;
            for (int w = 0; w < WORKSPACE_COUNT; w++) {
                int textWidth = getTextWidth(workspaceNames[w]);
                if (textWidth > maxTextWidth)
                    maxTextWidth = textWidth;
            }

            int wsWidth = maxTextWidth + (PADDING * 4);

            for (int w = 0; w < WORKSPACE_COUNT; w++) {
                if (ev->x >= x && ev->x < x + wsWidth) {
                    if (monitors[i].currentWorkspace != w) {
                        monitors[i].currentWorkspace = w;
                        updateClientVisibility();

                        if (focused && focused->monitor == i) {
                            focused = NULL;
                            XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
                            updateBorders();
                        }

                        SClient* windowInWorkspace = findVisibleClientInWorkspace(i, w);
                        if (windowInWorkspace) {
                            fprintf(stderr, "Focusing window in workspace %d after bar click\n", w);
                            focusClient(windowInWorkspace);
                        }

                        updateBars();
                    }
                    return;
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
        if (!client->isFloating && client->y < monitors[client->monitor].y + BAR_HEIGHT) {
            client->y = monitors[client->monitor].y + BAR_HEIGHT;
            XMoveWindow(display, client->window, client->x, client->y);
        }
        client = client->next;
    }
}

SSystrayIcon* systrayIcons = NULL;
Window        systrayWin   = None;

int           systrayIconSize = BAR_HEIGHT - 4;
int           systraySpacing  = 2;

int           createSystray(void) {
    if (!ENABLE_SYSTRAY)
        return 0;

    if (systrayWin != None)
        return 1;

    if (NET_SYSTEM_TRAY_OPCODE == None) {
        NET_SYSTEM_TRAY_OPCODE      = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
        NET_SYSTEM_TRAY_ORIENTATION = XInternAtom(display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
        NET_SYSTEM_TRAY_VISUAL      = XInternAtom(display, "_NET_SYSTEM_TRAY_VISUAL", False);
        XEMBED                      = XInternAtom(display, "_XEMBED", False);
        XEMBED_INFO                 = XInternAtom(display, "_XEMBED_INFO", False);
    }

    systrayWin = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, barBgColor);
    if (!systrayWin)
        return 0;

    char atomName[32];
    snprintf(atomName, sizeof(atomName), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(display));
    Atom systrayAtom = XInternAtom(display, atomName, False);

    if (XGetSelectionOwner(display, systrayAtom) != None) {
        XDestroyWindow(display, systrayWin);
        systrayWin = None;
        return 0;
    }

    XSetSelectionOwner(display, systrayAtom, systrayWin, CurrentTime);
    if (XGetSelectionOwner(display, systrayAtom) != systrayWin) {
        XDestroyWindow(display, systrayWin);
        systrayWin = None;
        return 0;
    }

    XClientMessageEvent ev;
    ev.type         = ClientMessage;
    ev.window       = root;
    ev.message_type = XInternAtom(display, "MANAGER", False);
    ev.format       = 32;
    ev.data.l[0]    = CurrentTime;
    ev.data.l[1]    = systrayAtom;
    ev.data.l[2]    = systrayWin;
    ev.data.l[3]    = 0;
    ev.data.l[4]    = 0;
    XSendEvent(display, root, False, StructureNotifyMask, (XEvent*)&ev);

    long orientation = 0;
    XChangeProperty(display, systrayWin, NET_SYSTEM_TRAY_ORIENTATION, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&orientation, 1);

    XSelectInput(display, systrayWin, StructureNotifyMask | SubstructureNotifyMask);

    return 1;
}

void handleSystrayClientMessage(XEvent* event) {
    if (!ENABLE_SYSTRAY || systrayWin == None)
        return;

    XClientMessageEvent* ev = &event->xclient;

    if (ev->message_type == NET_SYSTEM_TRAY_OPCODE && ev->format == 32) {
        if (ev->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            Window icon = ev->data.l[2];
            if (icon == None)
                return;

            SSystrayIcon* i;
            for (i = systrayIcons; i; i = i->next)
                if (i->win == icon)
                    return;

            SSystrayIcon* newIcon = malloc(sizeof(SSystrayIcon));
            if (!newIcon)
                return;

            newIcon->win  = icon;
            newIcon->next = systrayIcons;
            systrayIcons  = newIcon;

            if (barWindows) {
                SMonitor* activeMonitor = getCurrentMonitor();
                if (activeMonitor->num >= numMonitors || !barWindows[activeMonitor->num])
                    activeMonitor->num = 0;

                Window               parentWindow = barWindows[activeMonitor->num];

                XSetWindowAttributes wa;
                wa.event_mask = StructureNotifyMask | PropertyChangeMask;
                XChangeWindowAttributes(display, icon, CWEventMask, &wa);

                long xembedInfo[2];
                xembedInfo[0] = 0;
                xembedInfo[1] = 1;
                XChangeProperty(display, icon, XEMBED_INFO, XEMBED_INFO, 32, PropModeReplace, (unsigned char*)xembedInfo, 2);

                XReparentWindow(display, icon, parentWindow, 0, 0);
                XMapRaised(display, icon);

                XClientMessageEvent xemEv;
                xemEv.type         = ClientMessage;
                xemEv.window       = icon;
                xemEv.message_type = XEMBED;
                xemEv.format       = 32;
                xemEv.data.l[0]    = CurrentTime;
                xemEv.data.l[1]    = XEMBED_EMBEDDED_NOTIFY;
                xemEv.data.l[2]    = 0;
                xemEv.data.l[3]    = systrayWin;
                xemEv.data.l[4]    = 0;
                XSendEvent(display, icon, False, NoEventMask, (XEvent*)&xemEv);

                updateSystray();
            }
        }
    }
}

void removeSystrayIcon(Window win) {
    SSystrayIcon** p = &systrayIcons;
    SSystrayIcon*  i;

    while (*p) {
        i = *p;
        if (i->win == win) {
            *p = i->next;
            free(i);
            break;
        }
        p = &i->next;
    }

    updateSystray();
}

void updateSystray(void) {
    if (!ENABLE_SYSTRAY || systrayWin == None)
        return;

    SMonitor* activeMonitor = getCurrentMonitor();
    if (!barWindows || !barDraws || activeMonitor->num >= numMonitors || !barWindows[activeMonitor->num])
        return;

    int           x = 0;
    SSystrayIcon* i;

    for (i = systrayIcons; i; i = i->next) {
        XWindowAttributes wa;
        if (!XGetWindowAttributes(display, i->win, &wa))
            continue;

        Window       currentParent = None;
        Window*      children      = NULL;
        unsigned int numChildren;

        if (XQueryTree(display, i->win, &wa.root, &currentParent, &children, &numChildren)) {
            if (children)
                XFree(children);

            if (currentParent != barWindows[activeMonitor->num])
                XReparentWindow(display, i->win, barWindows[activeMonitor->num], 0, 0);
        }

        XMoveResizeWindow(display, i->win, x, (BAR_HEIGHT - systrayIconSize) / 2, systrayIconSize, systrayIconSize);
        XMapWindow(display, i->win);
        x += systrayIconSize + systraySpacing;
    }

    updateBars();
}

int getSystrayWidth(void) {
    if (!ENABLE_SYSTRAY || systrayWin == None)
        return 0;

    int           width     = 0;
    int           iconCount = 0;
    SSystrayIcon* i;

    for (i = systrayIcons; i; i = i->next) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(display, i->win, &wa))
            iconCount++;
    }

    if (iconCount > 0)
        width = (systrayIconSize + systraySpacing) * iconCount;

    return width > 0 ? width : 0;
}

void cleanupSystray(void) {
    if (systrayWin == None)
        return;

    SSystrayIcon* i = systrayIcons;
    while (i) {
        SSystrayIcon* next = i->next;
        free(i);
        i = next;
    }
    systrayIcons = NULL;

    XDestroyWindow(display, systrayWin);
    systrayWin = None;
}