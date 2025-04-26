#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <signal.h>
#include <sys/wait.h>
#include <err.h>

#include "defs.h"
#include "config.h"
#include "bar.h"

#define MAX_CLIENTS 64

Display*        display;
Window          root;
SClient*        clients     = NULL;
SClient*        focused     = NULL;
SMonitor*       monitors    = NULL;
int             numMonitors = 0;
Cursor          normalCursor;
Cursor          moveCursor;
Cursor          resizeCursor;
SWindowMovement windowMovement   = {0, 0, NULL, 0};
SWindowResize   windowResize     = {0, 0, NULL, 0};
SMFactAdjust    mfactAdjust      = {0, 0, NULL};
SWindowSwap     windowSwap       = {0, 0, NULL, 0, NULL};
int             currentWorkspace = 0;

Atom            WM_PROTOCOLS;
Atom            WM_DELETE_WINDOW;
Atom            WM_STATE;
Atom            WM_TAKE_FOCUS;

Atom            NET_SUPPORTED;
Atom            NET_WM_NAME;
Atom            NET_SUPPORTING_WM_CHECK;
Atom            NET_CLIENT_LIST;
Atom            NET_NUMBER_OF_DESKTOPS;
Atom            NET_CURRENT_DESKTOP;
Atom            NET_DESKTOP_NAMES;
Atom            NET_ACTIVE_WINDOW;
Atom            NET_WM_STATE;
Atom            NET_WM_STATE_FULLSCREEN;
Atom            NET_WM_WINDOW_TYPE;
Atom            NET_WM_WINDOW_TYPE_DIALOG;
Atom            UTF8_STRING;
Window          wmcheckwin;

int             xerrorHandler(Display* dpy, XErrorEvent* ee) {
    if (ee->error_code == BadWindow || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
        (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) || (ee->request_code == X_GetGeometry && ee->error_code == BadDrawable)) {
        return 0;
    }

    char errorText[256];
    XGetErrorText(dpy, ee->error_code, errorText, sizeof(errorText));
    fprintf(stderr, "banana: X error: %s (0x%x) request %d\n", errorText, ee->error_code, ee->request_code);
    return 0;
}

int otherWmRunningHandler(Display* dpy, XErrorEvent* ee) {
    if (ee->error_code == BadAccess && ee->request_code == X_ChangeWindowAttributes) {
        fprintf(stderr, "banana: another window manager is already running\n");
        exit(1);
    }
    return xerrorHandler(dpy, ee);
}

void checkOtherWM() {
    XSetErrorHandler(otherWmRunningHandler);
    XSelectInput(display, root, SubstructureRedirectMask);
    XSync(display, False);
    XSetErrorHandler(xerrorHandler);
    XSelectInput(display, root, 0);
    XSync(display, False);
}

static void (*eventHandlers[LASTEvent])(XEvent*) = {
    [KeyPress] = handleKeyPress,           [ButtonPress] = handleButtonPress, [ButtonRelease] = handleButtonRelease,       [MotionNotify] = handleMotionNotify,
    [EnterNotify] = handleEnterNotify,     [MapRequest] = handleMapRequest,   [ConfigureRequest] = handleConfigureRequest, [UnmapNotify] = handleUnmapNotify,
    [DestroyNotify] = handleDestroyNotify, [Expose] = handleExpose,           [PropertyNotify] = handlePropertyNotify,     [ClientMessage] = handleClientMessage,
};

void scanExistingWindows() {
    Window       rootReturn, parentReturn;
    Window*      children;
    unsigned int numChildren;

    if (XQueryTree(display, root, &rootReturn, &parentReturn, &children, &numChildren)) {
        for (unsigned int i = 0; i < numChildren; i++) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(display, children[i], &wa) && !wa.override_redirect && wa.map_state == IsViewable)
                manageClient(children[i]);
        }

        if (children)
            XFree(children);
    }
}

void setupEWMH() {
    NET_SUPPORTED             = XInternAtom(display, "_NET_SUPPORTED", False);
    NET_WM_NAME               = XInternAtom(display, "_NET_WM_NAME", False);
    NET_SUPPORTING_WM_CHECK   = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
    NET_CLIENT_LIST           = XInternAtom(display, "_NET_CLIENT_LIST", False);
    NET_NUMBER_OF_DESKTOPS    = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
    NET_CURRENT_DESKTOP       = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    NET_DESKTOP_NAMES         = XInternAtom(display, "_NET_DESKTOP_NAMES", False);
    NET_ACTIVE_WINDOW         = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    NET_WM_STATE              = XInternAtom(display, "_NET_WM_STATE", False);
    NET_WM_STATE_FULLSCREEN   = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    NET_WM_WINDOW_TYPE        = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    NET_WM_WINDOW_TYPE_DIALOG = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    UTF8_STRING               = XInternAtom(display, "UTF8_STRING", False);

    wmcheckwin = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, root, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&wmcheckwin, 1);
    XChangeProperty(display, wmcheckwin, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&wmcheckwin, 1);
    XChangeProperty(display, wmcheckwin, NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (unsigned char*)"banana", 6);
    XChangeProperty(display, root, NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (unsigned char*)"banana", 6);

    Atom supported[] = {NET_SUPPORTED,     NET_WM_NAME,  NET_SUPPORTING_WM_CHECK, NET_CLIENT_LIST,    NET_NUMBER_OF_DESKTOPS,   NET_CURRENT_DESKTOP, NET_DESKTOP_NAMES,
                        NET_ACTIVE_WINDOW, NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_DIALOG};

    XChangeProperty(display, root, NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char*)supported, sizeof(supported) / sizeof(Atom));

    long numDesktops = WORKSPACE_COUNT;
    XChangeProperty(display, root, NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&numDesktops, 1);

    long currentDesktop = currentWorkspace;
    XChangeProperty(display, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&currentDesktop, 1);

    char workspaceNames[WORKSPACE_COUNT * 16] = {0};
    int  offset                               = 0;

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Workspace %d", i + 1);

        int nameLen = strlen(name) + 1;
        memcpy(workspaceNames + offset, name, nameLen);
        offset += nameLen;
    }

    XChangeProperty(display, root, NET_DESKTOP_NAMES, UTF8_STRING, 8, PropModeReplace, (unsigned char*)workspaceNames, offset);

    updateClientList();
}

void setup() {
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "banana: cannot open display\n");
        exit(1);
    }

    XSetErrorHandler(xerrorHandler);
    root = DefaultRootWindow(display);

    checkOtherWM();

    WM_PROTOCOLS     = XInternAtom(display, "WM_PROTOCOLS", False);
    WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);
    WM_STATE         = XInternAtom(display, "WM_STATE", False);
    WM_TAKE_FOCUS    = XInternAtom(display, "WM_TAKE_FOCUS", False);

    setupEWMH();

    normalCursor = XCreateFontCursor(display, XC_left_ptr);
    moveCursor   = XCreateFontCursor(display, XC_fleur);
    resizeCursor = XCreateFontCursor(display, XC_bottom_right_corner);
    XDefineCursor(display, root, normalCursor);

    XSetWindowAttributes wa;
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | PointerMotionMask |
                    ButtonMotionMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(display, root, CWEventMask, &wa);
    XSelectInput(display, root, wa.event_mask);

    fprintf(stderr, "Root window listening to events\n");

    updateMonitors();

    for (int i = 0; i < numMonitors; i++) {
        monitors[i].currentWorkspace = 0;
    }

    grabKeys();

    createBars();

    updateStatus();

    scanExistingWindows();

    updateClientPositionsForBar();
    updateClientVisibility();

    XSync(display, False);
}

void run() {
    XEvent event;

    XSync(display, False);
    fprintf(stderr, "Starting main event loop\n");

    updateClientVisibility();
    updateBars();

    while (XNextEvent(display, &event) == 0) {
        if (event.type == ButtonPress)
            fprintf(stderr, "Received ButtonPress event\n");

        if (eventHandlers[event.type]) {
            XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

            eventHandlers[event.type](&event);

            XSync(display, False);
            XSetErrorHandler(oldHandler);
        }
    }

    fprintf(stderr, "Event loop exited\n");
}

void cleanup() {
    cleanupBars();

    SClient* client = clients;
    SClient* next;
    while (client) {
        next = client->next;
        free(client);
        client = next;
    }

    free(monitors);

    XDestroyWindow(display, wmcheckwin);

    XFreeCursor(display, normalCursor);
    XFreeCursor(display, moveCursor);
    XFreeCursor(display, resizeCursor);

    XCloseDisplay(display);
}

void handleKeyPress(XEvent* event) {
    XKeyEvent* ev     = &event->xkey;
    KeySym     keysym = XLookupKeysym(ev, 0);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].keysym == keysym && keys[i].mod == ev->state) {
            keys[i].func(keys[i].arg);
            break;
        }
    }
}

void handleButtonPress(XEvent* event) {
    XButtonPressedEvent* ev            = &event->xbutton;
    Window               clickedWindow = ev->window;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows && barWindows[i] == clickedWindow) {
            handleBarClick(event);
            return;
        }
    }

    SClient* client = findClient(ev->window);

    if (ev->window == root && (ev->state & MODKEY) && ev->button == Button3) {
        SMonitor* monitor   = monitorAtPoint(ev->x_root, ev->y_root);
        mfactAdjust.monitor = monitor;
        mfactAdjust.x       = ev->x_root;
        mfactAdjust.active  = 1;
        XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, resizeCursor, CurrentTime);
        return;
    } else if (client && (ev->state & MODKEY) && !client->isFloating && ev->button == Button3) {
        SMonitor* monitor   = &monitors[client->monitor];
        mfactAdjust.monitor = monitor;
        mfactAdjust.x       = ev->x_root;
        mfactAdjust.active  = 1;
        XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, resizeCursor, CurrentTime);
        return;
    }

    if (!client || (ev->state & MODKEY) == 0)
        return;

    if (ev->button == Button1) {
        if (!client->isFloating) {
            windowSwap.client             = client;
            windowSwap.client->oldMonitor = client->monitor;
            windowSwap.x                  = ev->x_root;
            windowSwap.y                  = ev->y_root;
            windowSwap.active             = 1;
            windowSwap.lastTarget         = NULL;
            XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, moveCursor, CurrentTime);
        } else {
            windowMovement.client = client;
            windowMovement.x      = ev->x_root;
            windowMovement.y      = ev->y_root;
            windowMovement.active = 1;
            XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, moveCursor, CurrentTime);
        }
    } else if (ev->button == Button3 && client->isFloating) {
        windowResize.client = client;
        windowResize.x      = ev->x_root;
        windowResize.y      = ev->y_root;
        windowResize.active = 1;
        XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, resizeCursor, CurrentTime);
    }
}

void handleButtonRelease(XEvent* event) {
    XButtonEvent* ev = &event->xbutton;

    fprintf(stderr, "ButtonRelease: button=%d, state=0x%x\n", ev->button, ev->state);

    if (windowMovement.active && ev->button == Button1) {
        fprintf(stderr, "  Ending window movement\n");
        windowMovement.active = 0;
        windowMovement.client = NULL;
        XUngrabPointer(display, CurrentTime);
    }

    if (windowSwap.active && ev->button == Button1) {
        fprintf(stderr, "  Ending window swap\n");
        if (windowSwap.client && windowSwap.client->monitor != windowSwap.client->oldMonitor)
            fprintf(stderr, "  Window moved from monitor %d to monitor %d\n", windowSwap.client->oldMonitor, windowSwap.client->monitor);
        windowSwap.active     = 0;
        windowSwap.client     = NULL;
        windowSwap.lastTarget = NULL;
        XUngrabPointer(display, CurrentTime);
    }

    if (windowResize.active && ev->button == Button3) {
        fprintf(stderr, "  Ending window resize\n");
        windowResize.active = 0;
        windowResize.client = NULL;
        XUngrabPointer(display, CurrentTime);
    }

    if (mfactAdjust.active && ev->button == Button3) {
        fprintf(stderr, "  Ending mfact adjustment\n");
        mfactAdjust.active  = 0;
        mfactAdjust.monitor = NULL;
        XUngrabPointer(display, CurrentTime);
    }
}

SClient* clientAtPoint(int x, int y) {
    SClient*  client = clients;
    SMonitor* m      = monitorAtPoint(x, y);

    while (client) {
        if (client->monitor == m->num && client->workspace == m->currentWorkspace && x >= client->x && x < client->x + client->width && y >= client->y &&
            y < client->y + client->height)
            return client;
        client = client->next;
    }

    return NULL;
}

void handleMotionNotify(XEvent* event) {
    XMotionEvent* ev = &event->xmotion;

    while (XCheckTypedWindowEvent(display, ev->window, MotionNotify, event))
        ;

    if (windowMovement.active && windowMovement.client) {
        int dx = ev->x_root - windowMovement.x;
        int dy = ev->y_root - windowMovement.y;

        moveWindow(windowMovement.client, windowMovement.client->x + dx, windowMovement.client->y + dy);

        windowMovement.x = ev->x_root;
        windowMovement.y = ev->y_root;
    } else if (windowSwap.active && windowSwap.client) {
        SMonitor* targetMonitor = monitorAtPoint(ev->x_root, ev->y_root);
        SClient*  targetClient  = clientAtPoint(ev->x_root, ev->y_root);

        if (targetMonitor->num != windowSwap.client->monitor) {
            int prevMonitor              = windowSwap.client->monitor;
            windowSwap.client->monitor   = targetMonitor->num;
            windowSwap.client->workspace = targetMonitor->currentWorkspace;

            arrangeClients(&monitors[prevMonitor]);
            arrangeClients(targetMonitor);

            focusClient(windowSwap.client);

            windowSwap.x = ev->x_root;
            windowSwap.y = ev->y_root;

            fprintf(stderr, "Window moved to monitor %d\n", targetMonitor->num);
        } else if (targetClient && targetClient != windowSwap.client && !targetClient->isFloating && targetClient->monitor == windowSwap.client->monitor &&
                   targetClient->workspace == windowSwap.client->workspace) {
            swapClients(windowSwap.client, targetClient);

            SMonitor* monitor = &monitors[windowSwap.client->monitor];
            arrangeClients(monitor);

            windowSwap.x          = ev->x_root;
            windowSwap.y          = ev->y_root;
            windowSwap.lastTarget = targetClient;
        }
    } else if (windowResize.active && windowResize.client) {
        int dx = ev->x_root - windowResize.x;
        int dy = ev->y_root - windowResize.y;

        resizeWindow(windowResize.client, windowResize.client->width + dx, windowResize.client->height + dy);

        windowResize.x = ev->x_root;
        windowResize.y = ev->y_root;
    } else if (mfactAdjust.active && mfactAdjust.monitor) {
        int   dx = ev->x_root - mfactAdjust.x;

        float delta     = (float)dx / mfactAdjust.monitor->width * 0.95;
        int   workspace = mfactAdjust.monitor->currentWorkspace;

        mfactAdjust.monitor->masterFactors[workspace] += delta;

        if (mfactAdjust.monitor->masterFactors[workspace] < 0.1)
            mfactAdjust.monitor->masterFactors[workspace] = 0.1;
        else if (mfactAdjust.monitor->masterFactors[workspace] > 0.9)
            mfactAdjust.monitor->masterFactors[workspace] = 0.9;

        tileClients(mfactAdjust.monitor);
        for (int m = 0; m < numMonitors; m++) {
            SMonitor* monitor = &monitors[m];
            if (monitor->num == mfactAdjust.monitor->num) {
                for (SClient* c = clients; c; c = c->next) {
                    if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && !c->isFloating)
                        XLowerWindow(display, c->window);
                }
            }
        }

        mfactAdjust.x = ev->x_root;
    } else {
        SMonitor* currentMonitor    = monitorAtPoint(ev->x_root, ev->y_root);
        SClient*  clientUnderCursor = clientAtPoint(ev->x_root, ev->y_root);

        if (clientUnderCursor) {
            if (focused != clientUnderCursor) {
                fprintf(stderr, "Cursor over window on monitor %d, focusing\n", currentMonitor->num);
                focusClient(clientUnderCursor);
            }
        } else if (focused && (focused->monitor != currentMonitor->num || !clientAtPoint(ev->x_root, ev->y_root))) {
            int activeMonitor = -1;
            if (focused)
                activeMonitor = focused->monitor;

            SClient* clientInWorkspace = findVisibleClientInWorkspace(currentMonitor->num, currentMonitor->currentWorkspace);

            if (activeMonitor != currentMonitor->num || !clientInWorkspace) {
                fprintf(stderr, "Cursor not over any window on monitor %d, focusing monitor\n", currentMonitor->num);
                XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
                focused = NULL;
                updateBorders();
                updateBars();
            }
        }
    }
}

void moveWindow(SClient* client, int x, int y) {
    if (!client)
        return;

    if (!client->isFloating)
        return;

    if (client->isFullscreen)
        return;

    int prevMonitor = client->monitor;

    client->x = x;
    client->y = y;

    SMonitor* monitor = &monitors[client->monitor];
    int       centerX = client->x + client->width / 2;
    int       centerY = client->y + client->height / 2;
    monitor           = monitorAtPoint(centerX, centerY);
    client->monitor   = monitor->num;

    XMoveWindow(display, client->window, client->x, client->y);

    configureClient(client);

    if (prevMonitor != client->monitor) {
        fprintf(stderr, "Window moved to a different monitor, updating layout\n");

        if (windowMovement.active && windowMovement.client == client) {
            fprintf(stderr, "Window being dragged to different monitor, focusing it\n");
            focusClient(client);
        }

        arrangeClients(&monitors[prevMonitor]);
        arrangeClients(monitor);
    }

    if (windowMovement.active && windowMovement.client == client)
        XRaiseWindow(display, client->window);
}

void resizeWindow(SClient* client, int width, int height) {
    if (!client)
        return;

    if (!client->isFloating)
        return;
    if (client->isFullscreen) {
        SMonitor* monitor = &monitors[client->monitor];
        client->x         = monitor->x;
        client->y         = monitor->y;
        client->width     = monitor->width;
        client->height    = monitor->height;

        XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
        configureClient(client);
        return;
    }

    if (client->sizeHints.valid) {
        if (client->sizeHints.minWidth > 0 && width < client->sizeHints.minWidth)
            width = client->sizeHints.minWidth;
        if (client->sizeHints.minHeight > 0 && height < client->sizeHints.minHeight)
            height = client->sizeHints.minHeight;

        if (client->sizeHints.maxWidth > 0 && width > client->sizeHints.maxWidth)
            width = client->sizeHints.maxWidth;
        if (client->sizeHints.maxHeight > 0 && height > client->sizeHints.maxHeight)
            height = client->sizeHints.maxHeight;
    } else {
        if (width < 20)
            width = 20;
        if (height < 20)
            height = 20;
    }

    client->width  = width;
    client->height = height;

    XResizeWindow(display, client->window, client->width, client->height);

    if (windowResize.active && windowResize.client == client)
        XRaiseWindow(display, client->window);

    configureClient(client);
}

void handleEnterNotify(XEvent* event) {
    XCrossingEvent* ev = &event->xcrossing;

    if (windowMovement.active || windowResize.active)
        return;

    if (ev->mode != NotifyNormal || ev->detail == NotifyInferior)
        return;

    SClient* client = findClient(ev->window);
    if (client) {
        SMonitor* monitor = &monitors[client->monitor];

        if (client->workspace == monitor->currentWorkspace && client != focused) {
            fprintf(stderr, "Focusing window 0x%lx after enter notify\n", ev->window);
            focusClient(client);
        }
    }
}

void handleMapRequest(XEvent* event) {
    XMapRequestEvent* ev = &event->xmaprequest;

    manageClient(ev->window);

    SClient* client = findClient(ev->window);
    if (client) {
        SMonitor* monitor = &monitors[client->monitor];
        Atom      state   = getAtomProperty(client, NET_WM_STATE);
        if (state == NET_WM_STATE_FULLSCREEN) {
            fprintf(stderr, "Detected fullscreen window during map request, forcing proper position\n");

            client->oldx      = client->x;
            client->oldy      = client->y;
            client->oldwidth  = client->width;
            client->oldheight = client->height;

            client->x            = monitor->x;
            client->y            = monitor->y;
            client->width        = monitor->width;
            client->height       = monitor->height;
            client->isFullscreen = 1;
            client->isFloating   = 1;

            XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
            XSetWindowBorderWidth(display, client->window, 0);
            configureClient(client);
            XRaiseWindow(display, client->window);
        }

        if (client->workspace == monitor->currentWorkspace)
            XMapWindow(display, ev->window);
        else
            XUnmapWindow(display, ev->window);

        if (!client->isFloating && !client->isFullscreen) {
            XSync(display, False);
            arrangeClients(monitor);
        }
    } else
        XMapWindow(display, ev->window);
}

void handleConfigureRequest(XEvent* event) {
    XConfigureRequestEvent* ev = &event->xconfigurerequest;
    XWindowChanges          wc;
    wc.x            = ev->x;
    wc.y            = ev->y;
    wc.width        = ev->width;
    wc.height       = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling      = ev->above;
    wc.stack_mode   = ev->detail;

    SClient* client = findClient(ev->window);
    if (client) {
        if (client->isFullscreen) {
            fprintf(stderr, "Intercepting configure request for fullscreen window\n");
            SMonitor* monitor = &monitors[client->monitor];
            wc.x              = monitor->x;
            wc.y              = monitor->y;
            wc.width          = monitor->width;
            wc.height         = monitor->height;
            wc.border_width   = 0;
        } else if (!client->isFloating) {
            fprintf(stderr, "Intercepting configure request for tiled window\n");
            wc.x            = client->x;
            wc.y            = client->y;
            wc.width        = client->width;
            wc.height       = client->height;
            wc.border_width = BORDER_WIDTH;
        }
    }

    XConfigureWindow(display, ev->window, ev->value_mask, &wc);

    if (client)
        configureClient(client);
}

void handleUnmapNotify(XEvent* event) {
    XUnmapEvent* ev = &event->xunmap;

    if (ev->send_event) {
        SClient* client = findClient(ev->window);
        if (client)
            unmanageClient(ev->window);
    }
}

void handleDestroyNotify(XEvent* event) {
    XDestroyWindowEvent* ev     = &event->xdestroywindow;
    SClient*             client = findClient(ev->window);
    if (client)
        unmanageClient(ev->window);
}

void spawnProgram(const char* program) {
    if (!program)
        return;

    if (fork() == 0) {
        if (display)
            close(ConnectionNumber(display));
        setsid();
        execl("/bin/sh", "sh", "-c", program, NULL);
        exit(EXIT_SUCCESS);
    }
}

void killClient(const char* arg) {
    (void)arg;

    if (!focused)
        return;

    fprintf(stderr, "Killing client 0x%lx\n", focused->window);

    if (!sendEvent(focused, WM_DELETE_WINDOW)) {
        XGrabServer(display);
        XSetErrorHandler(xerrorHandler);
        XSetCloseDownMode(display, DestroyAll);
        XKillClient(display, focused->window);
        XSync(display, False);
        XUngrabServer(display);
    }
}

void quit(const char* arg) {
    int exitCode = EXIT_SUCCESS;

    if (arg && *arg) {
        char* endptr;
        long  parsedCode = strtol(arg, &endptr, 10);

        if (*endptr == '\0' && parsedCode >= 0 && parsedCode <= 255)
            exitCode = (int)parsedCode;
    }

    cleanup();
    exit(exitCode);
}

void grabKeys() {
    XUngrabKey(display, AnyKey, AnyModifier, root);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        XGrabKey(display, XKeysymToKeycode(display, keys[i].keysym), keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
    }

    fprintf(stderr, "Key grabs set up on root window\n");
}

void updateFocus() {
    if (!focused) {
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        return;
    }

    XSetInputFocus(display, focused->window, RevertToPointerRoot, CurrentTime);
    updateBorders();
}

void focusClient(SClient* client) {
    if (!client)
        return;

    fprintf(stderr, "Attempting to focus: 0x%lx\n", client->window);

    XWindowAttributes wa;
    if (!XGetWindowAttributes(display, client->window, &wa)) {
        fprintf(stderr, "  Window no longer exists\n");
        return;
    }

    if (wa.map_state != IsViewable) {
        fprintf(stderr, "  Window not viewable (state: %d)\n", wa.map_state);
        return;
    }

    if (wa.override_redirect) {
        fprintf(stderr, "  Window has override_redirect set\n");
        return;
    }

    if (focused && focused != client)
        XSetWindowBorder(display, focused->window, 0x444444);

    focused = client;

    XSetWindowBorder(display, client->window, 0xFF0000);
    XRaiseWindow(display, client->window);

    if (!client->neverfocus) {
        XSetInputFocus(display, client->window, RevertToPointerRoot, CurrentTime);
        XChangeProperty(display, root, NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&client->window, 1);
    }

    sendEvent(client, WM_TAKE_FOCUS);
    client->isUrgent = 0;

    updateBorders();
    restackFloatingWindows();
    updateBars();
}

void manageClient(Window window) {
    if (findClient(window))
        return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(display, window, &wa)) {
        fprintf(stderr, "Cannot manage window 0x%lx: failed to get attributes\n", window);
        return;
    }

    if (wa.override_redirect) {
        fprintf(stderr, "Skipping override_redirect window 0x%lx\n", window);
        return;
    }

    SClient* client = malloc(sizeof(SClient));
    if (!client) {
        fprintf(stderr, "Failed to allocate memory for client\n");
        return;
    }

    int          monitorNum = 0;
    int          cursorX    = 0;
    int          cursorY    = 0;

    int          rootX, rootY;
    unsigned int mask;
    Window       root_return, child_return;
    Bool         pointerQuerySuccess = False;

    if (XQueryPointer(display, root, &root_return, &child_return, &rootX, &rootY, &cursorX, &cursorY, &mask))
        pointerQuerySuccess = True;

    if (focused) {
        monitorNum = focused->monitor;
        fprintf(stderr, "Using focused monitor %d for new window\n", monitorNum);
    } else if (pointerQuerySuccess) {
        monitorNum = monitorAtPoint(rootX, rootY)->num;
        fprintf(stderr, "Using monitor %d at cursor position for new window\n", monitorNum);
    }

    client->monitor         = monitorNum;
    client->workspace       = monitors[monitorNum].currentWorkspace;
    client->window          = window;
    client->isFloating      = 0;
    client->isFullscreen    = 0;
    client->isUrgent        = 0;
    client->neverfocus      = 0;
    client->oldState        = 0;
    client->oldx            = 0;
    client->oldy            = 0;
    client->oldwidth        = 0;
    client->oldheight       = 0;
    client->sizeHints.valid = 0;

    SMonitor* monitor = &monitors[client->monitor];

    updateSizeHints(client);

    if (wa.width > monitor->width - 2 * BORDER_WIDTH)
        client->width = monitor->width - 2 * BORDER_WIDTH;
    else
        client->width = wa.width;

    if (wa.height > monitor->height - 2 * BORDER_WIDTH)
        client->height = monitor->height - 2 * BORDER_WIDTH;
    else
        client->height = wa.height;

    if (client->sizeHints.valid && client->isFloating) {
        if (client->sizeHints.minWidth > 0 && client->width < client->sizeHints.minWidth)
            client->width = client->sizeHints.minWidth;
        if (client->sizeHints.minHeight > 0 && client->height < client->sizeHints.minHeight)
            client->height = client->sizeHints.minHeight;
    }

    client->x = monitor->x + (monitor->width - client->width) / 2;
    client->y = monitor->y + (monitor->height - client->height) / 2;

    if (client->y < monitor->y + BAR_HEIGHT && !client->isFloating)
        client->y = monitor->y + BAR_HEIGHT;

    applyRules(client);

    client->next = NULL;
    if (!clients)
        clients = client;
    else {
        SClient* last = clients;
        while (last->next)
            last = last->next;
        last->next = client;
    }

    XMoveResizeWindow(display, window, client->x, client->y, client->width, client->height);

    XSetWindowBorderWidth(display, window, BORDER_WIDTH);

    XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

    XSelectInput(display, window, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);

    XGrabButton(display, Button1, MODKEY, window, False, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, moveCursor);

    XGrabButton(display, Button3, MODKEY, window, False, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, resizeCursor);

    XSync(display, False);
    XSetErrorHandler(oldHandler);

    fprintf(stderr, "Client managed: 0x%lx on monitor %d at position %d,%d with size %dx%d\n", window, client->monitor, client->x, client->y, client->width, client->height);

    updateBorders();

    XChangeProperty(display, root, NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*)&window, 1);
    setClientState(client, NormalState);

    updateWindowType(client);
    updateWMHints(client);

    configureClient(client);

    if (wa.map_state == IsViewable) {
        fprintf(stderr, "Window is viewable, focusing now\n");
        focusClient(client);
    } else
        fprintf(stderr, "Window not yet viewable (state: %d), deferring focus\n", wa.map_state);

    arrangeClients(monitor);

    if (!client->isFloating && !client->isFullscreen)
        XLowerWindow(display, client->window);

    restackFloatingWindows();

    updateBars();

    updateClientList();
}

void unmanageClient(Window window) {
    SClient* client = clients;
    SClient* prev   = NULL;

    while (client && client->window != window) {
        prev   = client;
        client = client->next;
    }

    if (!client)
        return;

    if (client == focused) {
        SMonitor* currentMonitor   = &monitors[client->monitor];
        int       currentWorkspace = client->workspace;

        SClient*  nextClient = client->next;
        while (nextClient && (nextClient->monitor != client->monitor || nextClient->workspace != currentWorkspace)) {
            nextClient = nextClient->next;
        }

        if (!nextClient) {
            nextClient = clients;
            while (nextClient && nextClient != client && (nextClient->monitor != client->monitor || nextClient->workspace != currentWorkspace)) {
                nextClient = nextClient->next;
            }
            if (nextClient == client)
                nextClient = NULL;
        }

        if (nextClient) {
            fprintf(stderr, "Window closed, focusing next client in workspace\n");
            focused = nextClient;
        } else {
            fprintf(stderr, "Window closed, no other windows in workspace, focusing monitor %d\n", currentMonitor->num);
            focused = NULL;
        }

        updateFocus();
    }

    SMonitor* monitor = &monitors[client->monitor];

    if (prev)
        prev->next = client->next;
    else
        clients = client->next;

    free(client);

    arrangeClients(monitor);
    updateBars();

    updateClientList();
}

void configureClient(SClient* client) {
    if (!client)
        return;

    XWindowChanges wc;
    wc.x              = client->x;
    wc.y              = client->y;
    wc.width          = client->width;
    wc.height         = client->height;
    wc.border_width   = client->isFullscreen ? 0 : BORDER_WIDTH;
    unsigned int mask = CWX | CWY | CWWidth | CWHeight | CWBorderWidth;
    XConfigureWindow(display, client->window, mask, &wc);

    XConfigureEvent event;
    event.type              = ConfigureNotify;
    event.display           = display;
    event.event             = client->window;
    event.window            = client->window;
    event.x                 = client->x;
    event.y                 = client->y;
    event.width             = client->width;
    event.height            = client->height;
    event.border_width      = client->isFullscreen ? 0 : BORDER_WIDTH;
    event.above             = None;
    event.override_redirect = False;
    XSendEvent(display, client->window, False, StructureNotifyMask, (XEvent*)&event);

    fprintf(stderr, "Configure client 0x%lx: monitor=%d pos=%d,%d size=%dx%d border=%d\n", client->window, client->monitor, client->x, client->y, client->width, client->height,
            client->isFullscreen ? 0 : BORDER_WIDTH);
}

void updateBorders() {
    static unsigned long activeBorder   = 0;
    static unsigned long inactiveBorder = 0;

    if (activeBorder == 0 || inactiveBorder == 0) {
        XColor   color;
        Colormap cmap = DefaultColormap(display, DefaultScreen(display));

        if (XAllocNamedColor(display, cmap, ACTIVE_BORDER_COLOR, &color, &color))
            activeBorder = color.pixel;
        else
            activeBorder = BlackPixel(display, DefaultScreen(display));

        if (XAllocNamedColor(display, cmap, INACTIVE_BORDER_COLOR, &color, &color))
            inactiveBorder = color.pixel;
        else
            inactiveBorder = BlackPixel(display, DefaultScreen(display));

        fprintf(stderr, "Border colors initialized\n");
    }

    SClient* client = clients;
    while (client) {
        XSetWindowBorder(display, client->window, (client == focused) ? activeBorder : inactiveBorder);
        client = client->next;
    }
}

SClient* findClient(Window window) {
    SClient* client = clients;
    while (client) {
        if (client->window == window)
            return client;
        client = client->next;
    }
    return NULL;
}

SMonitor* monitorAtPoint(int x, int y) {
    if (numMonitors <= 1)
        return &monitors[0];

    for (int i = 0; i < numMonitors; i++) {
        if (x >= monitors[i].x && x < monitors[i].x + monitors[i].width && y >= monitors[i].y && y < monitors[i].y + monitors[i].height)
            return &monitors[i];
    }

    return &monitors[0];
}

void updateMonitors() {
    int* oldWorkspaces  = NULL;
    int  oldNumMonitors = numMonitors;

    if (monitors) {
        oldWorkspaces = malloc(sizeof(int) * numMonitors);
        for (int i = 0; i < numMonitors; i++) {
            oldWorkspaces[i] = monitors[i].currentWorkspace;
        }
    }

    free(monitors);
    monitors    = NULL;
    numMonitors = 0;

    if (XineramaIsActive(display)) {
        XineramaScreenInfo* info = XineramaQueryScreens(display, &numMonitors);
        if (numMonitors > 0) {
            monitors = malloc(sizeof(SMonitor) * numMonitors);
            if (monitors) {
                for (int i = 0; i < numMonitors; i++) {
                    monitors[i].x             = info[i].x_org;
                    monitors[i].y             = info[i].y_org;
                    monitors[i].width         = info[i].width;
                    monitors[i].height        = info[i].height;
                    monitors[i].num           = i;
                    monitors[i].currentLayout = LAYOUT_TILED;
                    monitors[i].masterCount   = DEFAULT_MASTER_COUNT;

                    if (oldWorkspaces && i < oldNumMonitors)
                        monitors[i].currentWorkspace = oldWorkspaces[i];
                    else
                        monitors[i].currentWorkspace = 0;

                    for (int ws = 0; ws < WORKSPACE_COUNT; ws++) {
                        monitors[i].masterFactors[ws] = DEFAULT_MASTER_FACTOR;
                    }
                }
            }
            XFree(info);
        }
    }

    if (numMonitors == 0) {
        numMonitors = 1;
        monitors    = malloc(sizeof(SMonitor));
        if (monitors) {
            monitors[0].x                = 0;
            monitors[0].y                = 0;
            monitors[0].width            = DisplayWidth(display, DefaultScreen(display));
            monitors[0].height           = DisplayHeight(display, DefaultScreen(display));
            monitors[0].num              = 0;
            monitors[0].currentLayout    = LAYOUT_TILED;
            monitors[0].masterCount      = DEFAULT_MASTER_COUNT;
            monitors[0].currentWorkspace = oldWorkspaces ? oldWorkspaces[0] : 0;

            for (int ws = 0; ws < WORKSPACE_COUNT; ws++) {
                monitors[0].masterFactors[ws] = DEFAULT_MASTER_FACTOR;
            }
        }
    }

    if (oldWorkspaces)
        free(oldWorkspaces);

    if (numMonitors > 0) {
        createBars();
        for (SClient* c = clients; c; c = c->next) {
            SMonitor* mon = &monitors[c->monitor];

            if (c->x < mon->x)
                c->x = mon->x;
            if (c->y < mon->y + BAR_HEIGHT)
                c->y = mon->y + BAR_HEIGHT;
            if (c->x + c->width > mon->x + mon->width)
                c->x = mon->x + mon->width - c->width;
            if (c->y + c->height > mon->y + mon->height)
                c->y = mon->y + mon->height - c->height;

            if (!c->isFloating && !c->isFullscreen)
                arrangeClients(mon);
            else {
                XMoveResizeWindow(display, c->window, c->x, c->y, c->width, c->height);
                configureClient(c);
            }
        }
        updateBars();
    }
}

void handlePropertyNotify(XEvent* event) {
    XPropertyEvent* ev = &event->xproperty;

    if (ev->window == root && ev->atom == XA_WM_NAME)
        updateStatus();
    else if (ev->atom == XInternAtom(display, "_NET_WM_NAME", False) || ev->atom == XA_WM_NAME) {
        SClient* client = findClient(ev->window);
        if (client)
            updateBars();
    }

    SClient* client = findClient(ev->window);
    if (client) {
        if (ev->atom == XA_WM_NORMAL_HINTS || ev->atom == XA_WM_HINTS)
            updateWMHints(client);
        else if (ev->atom == NET_WM_WINDOW_TYPE)
            updateWindowType(client);
        else if (ev->atom == NET_WM_STATE) {
            Atom state = getAtomProperty(client, NET_WM_STATE);
            if (state == NET_WM_STATE_FULLSCREEN)
                setFullscreen(client, 1);
            else if (client->isFullscreen)
                setFullscreen(client, 0);
        }
    }
}

void handleExpose(XEvent* event) {
    XExposeEvent* ev = &event->xexpose;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows && barWindows[i] == ev->window) {
            handleBarExpose(event);
            return;
        }
    }
}

SClient* focusWindowUnderCursor(SMonitor* monitor) {
    int          x, y;
    unsigned int mask;
    Window       root_return, child_return;

    if (XQueryPointer(display, root, &root_return, &child_return, &x, &y, &x, &y, &mask)) {
        if (x >= monitor->x && x < monitor->x + monitor->width && y >= monitor->y && y < monitor->y + monitor->height) {

            SClient* windowUnderCursor = clientAtPoint(x, y);

            if (windowUnderCursor) {
                focusClient(windowUnderCursor);
                updateBars();
                return windowUnderCursor;
            }
        }
    }

    int activeMonitor = -1;
    if (focused)
        activeMonitor = focused->monitor;

    SClient* clientInWorkspace = findVisibleClientInWorkspace(monitor->num, monitor->currentWorkspace);

    if (activeMonitor != monitor->num || !clientInWorkspace) {
        focused = NULL;
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        updateBorders();
        updateBars();
    }
    return NULL;
}

void switchToWorkspace(const char* arg) {
    int workspace = atoi(arg);
    if (workspace < 0 || workspace >= WORKSPACE_COUNT)
        return;

    SMonitor* monitor = getCurrentMonitor();
    if (monitor->currentWorkspace == workspace)
        return;

    fprintf(stderr, "Switching from workspace %d to %d on monitor %d\n", monitor->currentWorkspace, workspace, monitor->num);

    monitor->currentWorkspace = workspace;

    long currentDesktop = workspace;
    XChangeProperty(display, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&currentDesktop, 1);

    updateClientVisibility();
    updateBars();

    arrangeClients(monitor);

    SClient* focusedClient = focusWindowUnderCursor(monitor);

    if (!focusedClient) {
        SClient* windowInWorkspace = findVisibleClientInWorkspace(monitor->num, workspace);
        if (windowInWorkspace) {
            fprintf(stderr, "No window under cursor, focusing available window in workspace %d\n", workspace);
            focusClient(windowInWorkspace);
        }
    }
}

void moveClientToWorkspace(const char* arg) {
    if (!arg || !focused)
        return;

    int workspace = atoi(arg);
    if (workspace < 0 || workspace >= WORKSPACE_COUNT)
        return;

    SMonitor* currentMon  = &monitors[focused->monitor];
    SClient*  movedClient = focused;

    movedClient->workspace = workspace;

    if (workspace != currentMon->currentWorkspace) {
        XUnmapWindow(display, movedClient->window);

        SClient* focusedClient = focusWindowUnderCursor(currentMon);

        if (!focusedClient) {
            SClient* remainingWindow = findVisibleClientInWorkspace(currentMon->num, currentMon->currentWorkspace);
            if (remainingWindow) {
                fprintf(stderr, "No window under cursor, focusing remaining window in workspace %d\n", currentMon->currentWorkspace);
                focusClient(remainingWindow);
            }
        }
    }

    arrangeClients(currentMon);

    for (int i = 0; i < numMonitors; i++) {
        if (i != currentMon->num && monitors[i].currentWorkspace == workspace) {
            arrangeClients(&monitors[i]);
            break;
        }
    }

    updateBars();
}

void updateClientVisibility() {
    SClient* client = clients;

    while (client) {
        SMonitor* m = &monitors[client->monitor];

        if (client->workspace == m->currentWorkspace)
            XMapWindow(display, client->window);
        else
            XUnmapWindow(display, client->window);
        client = client->next;
    }

    restackFloatingWindows();
}

SClient* findVisibleClientInWorkspace(int monitor, int workspace) {
    SClient* client = clients;

    while (client) {
        if (client->monitor == monitor && client->workspace == workspace)
            return client;
        client = client->next;
    }

    return NULL;
}

SMonitor* getCurrentMonitor() {
    int          x, y;
    unsigned int mask;
    Window       root_return, child_return;

    if (XQueryPointer(display, root, &root_return, &child_return, &x, &y, &x, &y, &mask))
        return monitorAtPoint(x, y);

    return &monitors[0];
}

void toggleFloating(const char* arg) {
    (void)arg;

    if (!focused)
        return;

    if (focused->isFullscreen)
        return;

    int wasFloating = focused->isFloating;

    focused->isFloating = !focused->isFloating;

    if (focused->isFloating) {
        SMonitor* monitor = &monitors[focused->monitor];

        if (monitor->currentLayout == LAYOUT_TILED) {
            int newWidth  = MAX(400, focused->width);
            int newHeight = MAX(300, focused->height);

            if (focused->sizeHints.valid) {
                if (focused->sizeHints.minWidth > 0 && newWidth < focused->sizeHints.minWidth)
                    newWidth = focused->sizeHints.minWidth;
                if (focused->sizeHints.minHeight > 0 && newHeight < focused->sizeHints.minHeight)
                    newHeight = focused->sizeHints.minHeight;
            }

            focused->x      = monitor->x + (monitor->width - newWidth) / 2;
            focused->y      = monitor->y + (monitor->height - newHeight) / 2;
            focused->width  = newWidth;
            focused->height = newHeight;

            XMoveResizeWindow(display, focused->window, focused->x, focused->y, focused->width, focused->height);
        }

        XRaiseWindow(display, focused->window);
        if (!focused->neverfocus) {
            XSetInputFocus(display, focused->window, RevertToPointerRoot, CurrentTime);
            XChangeProperty(display, root, NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&focused->window, 1);
        }
    } else if (wasFloating) {
        XLowerWindow(display, focused->window);
        SMonitor* newMonitor = monitorAtPoint(focused->x + focused->width / 2, focused->y + focused->height / 2);

        if (newMonitor->num != focused->monitor) {
            focused->monitor   = newMonitor->num;
            focused->workspace = newMonitor->currentWorkspace;
        }
    }

    arrangeClients(&monitors[focused->monitor]);
    updateBorders();

    restackFloatingWindows();

    updateBars();
}

void arrangeClients(SMonitor* monitor) {
    if (!monitor)
        return;

    switch (monitor->currentLayout) {
        case LAYOUT_TILED: tileClients(monitor); break;
        case LAYOUT_FLOATING: break;
        default: break;
    }

    restackFloatingWindows();
}

void restackFloatingWindows() {
    for (int m = 0; m < numMonitors; m++) {
        SMonitor* monitor = &monitors[m];

        for (SClient* c = clients; c; c = c->next) {
            if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && !c->isFloating)
                XLowerWindow(display, c->window);
        }

        for (SClient* c = clients; c; c = c->next) {
            if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && c->isFloating) {
                if (c == focused && (windowMovement.active || windowResize.active))
                    XRaiseWindow(display, c->window);
            }
        }
    }
}

void tileClients(SMonitor* monitor) {
    if (!monitor)
        return;

    int      currentWorkspace = monitor->currentWorkspace;

    int      visibleCount = 0;
    SClient* client       = clients;
    while (client) {
        if (client->monitor == monitor->num && client->workspace == monitor->currentWorkspace && !client->isFloating && !client->isFullscreen)
            visibleCount++;
        client = client->next;
    }

    if (visibleCount <= 1)
        monitor->masterFactors[currentWorkspace] = DEFAULT_MASTER_FACTOR;

    float masterFactor = monitor->masterFactors[currentWorkspace];

    int   x               = monitor->x + OUTER_GAP;
    int   y               = monitor->y + BAR_HEIGHT + OUTER_GAP;
    int   availableWidth  = monitor->width - (2 * OUTER_GAP);
    int   availableHeight = monitor->height - BAR_HEIGHT - (2 * OUTER_GAP);

    int   masterArea = availableWidth * masterFactor;
    int   stackArea  = availableWidth - masterArea;

    if (visibleCount == 0)
        return;

    if (visibleCount == 1) {
        client = clients;
        while (client) {
            if (client->monitor == monitor->num && client->workspace == monitor->currentWorkspace && !client->isFloating && !client->isFullscreen) {
                int width  = availableWidth - 2 * BORDER_WIDTH;
                int height = availableHeight - 2 * BORDER_WIDTH;

                client->x      = x;
                client->y      = y;
                client->width  = width;
                client->height = height;

                XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
                configureClient(client);
                fprintf(stderr, "Single window tiled: monitor=%d pos=%d,%d size=%dx%d\n", monitor->num, client->x, client->y, client->width, client->height);
                return;
            }
            client = client->next;
        }
    }

    int masterCount = MIN(monitor->masterCount, visibleCount);
    int stackCount  = visibleCount - masterCount;

    int masterHeight = 0;
    if (masterCount > 0)
        masterHeight = availableHeight / masterCount;

    int stackHeight = 0;
    if (stackCount > 0)
        stackHeight = availableHeight / stackCount;

    int masterRemainder = availableHeight % masterCount;
    int stackRemainder  = availableHeight % stackCount;

    client            = clients;
    int currentMaster = 0;
    int currentStack  = 0;
    int masterY       = y;
    int stackY        = y;

    int masterWidth = masterArea - INNER_GAP / 2 - 2 * BORDER_WIDTH;
    int stackWidth  = stackArea - INNER_GAP / 2 - 2 * BORDER_WIDTH;
    int stackX      = x + masterArea + INNER_GAP / 2;

    while (client) {
        if (client->monitor == monitor->num && client->workspace == monitor->currentWorkspace && !client->isFloating && !client->isFullscreen) {
            if (currentMaster < masterCount) {
                int heightAdjustment = (currentMaster < masterRemainder) ? 1 : 0;
                int currentHeight    = masterHeight + heightAdjustment;

                if (currentMaster > 0) {
                    masterY += INNER_GAP;
                    currentHeight -= INNER_GAP;
                }

                int width  = masterWidth;
                int height = currentHeight - 2 * BORDER_WIDTH;

                client->x      = x;
                client->y      = masterY;
                client->width  = width;
                client->height = height;

                masterY += currentHeight;
                currentMaster++;
            } else {
                int heightAdjustment = (currentStack < stackRemainder) ? 1 : 0;
                int currentHeight    = stackHeight + heightAdjustment;

                if (currentStack > 0) {
                    stackY += INNER_GAP;
                    currentHeight -= INNER_GAP;
                }

                int width  = stackWidth;
                int height = currentHeight - 2 * BORDER_WIDTH;

                client->x      = stackX;
                client->y      = stackY;
                client->width  = width;
                client->height = height;

                stackY += currentHeight;
                currentStack++;
            }

            XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
            configureClient(client);
            fprintf(stderr, "Tiling window: monitor=%d pos=%d,%d size=%dx%d\n", monitor->num, client->x, client->y, client->width, client->height);
        }

        client = client->next;
    }
}

void warpPointerToClientCenter(SClient* client) {
    if (!client)
        return;

    int centerX = client->x + client->width / 2;
    int centerY = client->y + client->height / 2;

    XWarpPointer(display, None, root, 0, 0, 0, 0, centerX, centerY);
}

void moveWindowInStack(const char* arg) {
    if (!focused || !arg)
        return;

    if (focused->isFloating)
        return;

    SMonitor* monitor   = &monitors[focused->monitor];
    int       workspace = focused->workspace;

    SClient*  prev         = NULL;
    SClient*  client       = clients;
    SClient*  targetClient = NULL;

    while (client && client != focused) {
        if (client->monitor == focused->monitor && client->workspace == workspace)
            prev = client;
        client = client->next;
    }

    if (strcmp(arg, "up") == 0) {
        if (prev) {
            targetClient = prev;

            SClient* prevPrev = NULL;
            client            = clients;
            while (client && client != prev) {
                if (client->monitor == focused->monitor && client->workspace == workspace)
                    prevPrev = client;
                client = client->next;
            }

            if (prevPrev)
                prevPrev->next = focused;
            else
                clients = focused;

            SClient* focusedNext = focused->next;
            focused->next        = prev;
            prev->next           = focusedNext;
        }
    } else if (strcmp(arg, "down") == 0) {
        targetClient = focused->next;
        while (targetClient && (targetClient->monitor != focused->monitor || targetClient->workspace != workspace)) {
            targetClient = targetClient->next;
        }

        if (targetClient) {
            focused->next      = targetClient->next;
            targetClient->next = focused;

            if (prev)
                prev->next = targetClient;
            else
                clients = targetClient;
        }
    }

    if (targetClient) {
        arrangeClients(monitor);
        restackFloatingWindows();

        warpPointerToClientCenter(focused);

        updateBorders();
    }
}

void focusWindowInStack(const char* arg) {
    if (!focused || !arg)
        return;

    int      workspace = focused->workspace;

    SClient* prev         = NULL;
    SClient* client       = clients;
    SClient* targetClient = NULL;

    while (client && client != focused) {
        if (client->monitor == focused->monitor && client->workspace == workspace)
            prev = client;
        client = client->next;
    }

    if (strcmp(arg, "up") == 0)
        targetClient = prev;
    else if (strcmp(arg, "down") == 0) {
        targetClient = focused->next;
        while (targetClient && (targetClient->monitor != focused->monitor || targetClient->workspace != workspace)) {
            targetClient = targetClient->next;
        }
    }

    if (targetClient) {
        focusClient(targetClient);
        warpPointerToClientCenter(targetClient);
    }
}

void adjustMasterFactor(const char* arg) {
    if (!arg)
        return;

    SMonitor* monitor   = getCurrentMonitor();
    int       workspace = monitor->currentWorkspace;
    float     delta     = 0.05;

    if (strcmp(arg, "decrease") == 0)
        delta = -delta;

    monitor->masterFactors[workspace] += delta;

    if (monitor->masterFactors[workspace] < 0.1)
        monitor->masterFactors[workspace] = 0.1;
    else if (monitor->masterFactors[workspace] > 0.9)
        monitor->masterFactors[workspace] = 0.9;

    tileClients(monitor);
    for (SClient* c = clients; c; c = c->next) {
        if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && !c->isFloating)
            XLowerWindow(display, c->window);
    }
}

void swapClients(SClient* a, SClient* b) {
    if (!a || !b || a == b)
        return;

    SClient* prevA = NULL;
    SClient* prevB = NULL;
    SClient* temp  = clients;

    while (temp && temp != a) {
        prevA = temp;
        temp  = temp->next;
    }

    temp = clients;
    while (temp && temp != b) {
        prevB = temp;
        temp  = temp->next;
    }

    if (!temp)
        return;

    if (prevA)
        prevA->next = b;
    else
        clients = b;

    if (prevB)
        prevB->next = a;
    else
        clients = a;

    temp    = a->next;
    a->next = b->next;
    b->next = temp;

    if (a->next == a)
        a->next = b;
    if (b->next == b)
        b->next = a;
}

void updateClientList() {
    Window   windowList[MAX_CLIENTS];
    int      count  = 0;
    SClient* client = clients;

    while (client && count < MAX_CLIENTS) {
        windowList[count++] = client->window;
        client              = client->next;
    }

    XChangeProperty(display, root, NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace, (unsigned char*)windowList, count);
}

Atom getAtomProperty(SClient* client, Atom prop) {
    int            di;
    unsigned long  dl;
    unsigned char* p = NULL;
    Atom           da, atom = None;

    if (XGetWindowProperty(display, client->window, prop, 0L, sizeof atom, False, XA_ATOM, &da, &di, &dl, &dl, &p) == Success && p) {
        atom = *(Atom*)p;
        XFree(p);
    }
    return atom;
}

void setClientState(SClient* client, long state) {
    long data[] = {state, None};

    XChangeProperty(display, client->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
}

int sendEvent(SClient* client, Atom proto) {
    int    n;
    Atom*  protocols;
    int    exists = 0;
    XEvent ev;

    if (XGetWMProtocols(display, client->window, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }

    if (exists) {
        ev.type                 = ClientMessage;
        ev.xclient.window       = client->window;
        ev.xclient.message_type = WM_PROTOCOLS;
        ev.xclient.format       = 32;
        ev.xclient.data.l[0]    = proto;
        ev.xclient.data.l[1]    = CurrentTime;
        XSendEvent(display, client->window, False, NoEventMask, &ev);
    }
    return exists;
}

void setFullscreen(SClient* client, int fullscreen) {
    if (fullscreen && !client->isFullscreen) {
        XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char*)&NET_WM_STATE_FULLSCREEN, 1);
        client->isFullscreen = 1;
        client->oldState     = client->isFloating;
        client->isFloating   = 1;

        client->oldx      = client->x;
        client->oldy      = client->y;
        client->oldwidth  = client->width;
        client->oldheight = client->height;

        XSetWindowBorderWidth(display, client->window, 0);

        SMonitor* monitor = &monitors[client->monitor];
        client->x         = monitor->x;
        client->y         = monitor->y;
        client->width     = monitor->width;
        client->height    = monitor->height;

        XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
        configureClient(client);
        XRaiseWindow(display, client->window);
    } else if (!fullscreen && client->isFullscreen) {
        XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char*)0, 0);
        client->isFullscreen = 0;
        client->isFloating   = client->oldState;

        client->x      = client->oldx;
        client->y      = client->oldy;
        client->width  = client->oldwidth;
        client->height = client->oldheight;

        XSetWindowBorderWidth(display, client->window, BORDER_WIDTH);

        XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
        configureClient(client);
        arrangeClients(&monitors[client->monitor]);
    }
}

void updateWindowType(SClient* client) {
    Atom state = getAtomProperty(client, NET_WM_STATE);
    Atom wtype = getAtomProperty(client, NET_WM_WINDOW_TYPE);

    fprintf(stderr, "Checking window type for 0x%lx, state=%ld, fullscreen=%ld\n", client->window, state, NET_WM_STATE_FULLSCREEN);

    if (state == NET_WM_STATE_FULLSCREEN) {
        fprintf(stderr, "Fullscreen window detected, forcing proper position\n");
        if (!client->isFullscreen) {
            client->oldx      = client->x;
            client->oldy      = client->y;
            client->oldwidth  = client->width;
            client->oldheight = client->height;

            SMonitor* monitor    = &monitors[client->monitor];
            client->isFullscreen = 1;
            client->oldState     = client->isFloating;
            client->isFloating   = 1;

            client->x      = monitor->x;
            client->y      = monitor->y;
            client->width  = monitor->width;
            client->height = monitor->height;

            XSetWindowBorderWidth(display, client->window, 0);
            XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char*)&NET_WM_STATE_FULLSCREEN, 1);

            XMoveResizeWindow(display, client->window, client->x, client->y, client->width, client->height);
            configureClient(client);
            XRaiseWindow(display, client->window);
        }
    }
    if (wtype == NET_WM_WINDOW_TYPE_DIALOG)
        client->isFloating = 1;
}

void updateWMHints(SClient* client) {
    XWMHints* wmh;

    if ((wmh = XGetWMHints(display, client->window))) {
        if (client == focused && wmh->flags & XUrgencyHint) {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(display, client->window, wmh);
        } else
            client->isUrgent = (wmh->flags & XUrgencyHint) ? 1 : 0;

        if (wmh->flags & InputHint)
            client->neverfocus = !wmh->input;
        else
            client->neverfocus = 0;

        XFree(wmh);
    }
}

void handleClientMessage(XEvent* event) {
    XClientMessageEvent* cme    = &event->xclient;
    SClient*             client = findClient(cme->window);

    if (!client)
        return;

    if (cme->message_type == NET_WM_STATE) {
        if (cme->data.l[1] == (long)NET_WM_STATE_FULLSCREEN || cme->data.l[2] == (long)NET_WM_STATE_FULLSCREEN)
            setFullscreen(client, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD */ || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !client->isFullscreen)));
    } else if (cme->message_type == NET_ACTIVE_WINDOW) {
        if (client != focused && !client->isUrgent) {
            client->isUrgent = 1;
            updateBorders();
            updateBars();

            SMonitor* m = &monitors[client->monitor];
            if (client->workspace != m->currentWorkspace)
                XUnmapWindow(display, client->window);
        }
    }
}

void toggleFullscreen(const char* arg) {
    (void)arg;

    if (!focused)
        return;

    setFullscreen(focused, !focused->isFullscreen);
}

void updateSizeHints(SClient* client) {
    XSizeHints hints;
    long       supplied;

    client->sizeHints.valid = 0;

    if (!XGetWMNormalHints(display, client->window, &hints, &supplied))
        return;

    if (supplied & PMinSize) {
        client->sizeHints.minWidth  = hints.min_width;
        client->sizeHints.minHeight = hints.min_height;
    } else {
        client->sizeHints.minWidth  = 0;
        client->sizeHints.minHeight = 0;
    }

    if (supplied & PMaxSize) {
        client->sizeHints.maxWidth  = hints.max_width;
        client->sizeHints.maxHeight = hints.max_height;
    } else {
        client->sizeHints.maxWidth  = 0;
        client->sizeHints.maxHeight = 0;
    }

    if (supplied & PBaseSize) {
        client->sizeHints.baseWidth  = hints.base_width;
        client->sizeHints.baseHeight = hints.base_height;
    } else {
        client->sizeHints.baseWidth  = 0;
        client->sizeHints.baseHeight = 0;
    }

    client->sizeHints.valid = 1;

    fprintf(stderr, "Size hints for 0x%lx: min=%dx%d, max=%dx%d, base=%dx%d\n", client->window, client->sizeHints.minWidth, client->sizeHints.minHeight, client->sizeHints.maxWidth,
            client->sizeHints.maxHeight, client->sizeHints.baseWidth, client->sizeHints.baseHeight);
}

void getWindowClass(Window window, char* className, char* instanceName, size_t bufSize) {
    XClassHint classHint;

    className[0]    = '\0';
    instanceName[0] = '\0';

    if (XGetClassHint(display, window, &classHint)) {
        if (classHint.res_class) {
            strncpy(className, classHint.res_class, bufSize - 1);
            className[bufSize - 1] = '\0';
            XFree(classHint.res_class);
        }

        if (classHint.res_name) {
            strncpy(instanceName, classHint.res_name, bufSize - 1);
            instanceName[bufSize - 1] = '\0';
            XFree(classHint.res_name);
        }
    }
}

int applyRules(SClient* client) {
    char className[256]    = {0};
    char instanceName[256] = {0};
    char title[256]        = {0};
    int  matched           = 0;

    getWindowClass(client->window, className, instanceName, sizeof(className));

    XTextProperty textProp;
    if (XGetTextProperty(display, client->window, &textProp, NET_WM_NAME) || XGetTextProperty(display, client->window, &textProp, XA_WM_NAME)) {
        strncpy(title, (char*)textProp.value, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
        XFree(textProp.value);
    }

    fprintf(stderr, "Checking rules for window: class='%s' instance='%s' title='%s'\n", className, instanceName, title);

    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        const char* ruleClass    = rules[i].className;
        const char* ruleInstance = rules[i].instanceName;
        const char* ruleTitle    = rules[i].title;

        if ((ruleClass == NULL || strcmp(className, ruleClass) == 0) && (ruleInstance == NULL || strcmp(instanceName, ruleInstance) == 0) &&
            (ruleTitle == NULL || (title[0] != '\0' && strstr(title, ruleTitle) != NULL))) {

            if (rules[i].isFloating != -1) {
                client->isFloating = rules[i].isFloating;
                matched            = 1;

                if (client->isFloating == 1) {
                    SMonitor* monitor = &monitors[client->monitor];

                    if (rules[i].width > 0) {
                        client->width = rules[i].width;
                        client->x     = monitor->x + (monitor->width - client->width) / 2;
                    }

                    if (rules[i].height > 0) {
                        client->height = rules[i].height;
                        client->y      = monitor->y + (monitor->height - client->height) / 2;
                    }

                    if (client->y < monitor->y + BAR_HEIGHT)
                        client->y = monitor->y + BAR_HEIGHT;
                }
            }

            if (rules[i].workspace != -1 && rules[i].workspace < WORKSPACE_COUNT) {
                client->workspace = rules[i].workspace;
                matched           = 1;
            }

            if (rules[i].monitor != -1 && rules[i].monitor < numMonitors) {
                client->monitor = rules[i].monitor;
                matched         = 1;
            }

            fprintf(stderr, "Applied rule %zu to window\n", i);
        }
    }

    return matched;
}

void focusMonitor(const char* arg) {
    if (!arg)
        return;

    SMonitor* currentMonitor = getCurrentMonitor();
    int       currentNum     = currentMonitor->num;
    int       targetMonitor  = currentNum;

    if (strcmp(arg, "right") == 0)
        targetMonitor = (currentNum + 1) % numMonitors;
    else if (strcmp(arg, "left") == 0)
        targetMonitor = (currentNum - 1 + numMonitors) % numMonitors;
    else {
        int monitorNum = atoi(arg);
        if (monitorNum >= 0 && monitorNum < numMonitors)
            targetMonitor = monitorNum;
    }

    if (targetMonitor == currentNum)
        return;

    SMonitor* monitor = &monitors[targetMonitor];

    int       centerX = monitor->x + monitor->width / 2;
    int       centerY = monitor->y + monitor->height / 2;

    XWarpPointer(display, None, root, 0, 0, 0, 0, centerX, centerY);

    SClient* clientToFocus = findVisibleClientInWorkspace(targetMonitor, monitor->currentWorkspace);

    if (clientToFocus) {
        focusClient(clientToFocus);
    } else {
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        focused = NULL;
        updateBorders();
    }

    updateBars();
}

int main() {
    signal(SIGCHLD, SIG_IGN);

    setup();

    updateClientList();

    run();

    cleanup();

    return 0;
}
