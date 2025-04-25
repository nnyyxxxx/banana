#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <err.h>

#include "defs.h"
#include "config.h"

Display*        display;
Window          root;
SClient*        clients     = NULL;
SClient*        focused     = NULL;
SMonitor*       monitors    = NULL;
int             numMonitors = 0;
Cursor          normalCursor;
Cursor          moveCursor;
Cursor          resizeCursor;
SWindowMovement windowMovement = {0, 0, NULL, 0};
SWindowResize   windowResize   = {0, 0, NULL, 0};

Atom            WM_PROTOCOLS;
Atom            WM_DELETE_WINDOW;

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
    [DestroyNotify] = handleDestroyNotify,
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
    grabKeys();

    scanExistingWindows();

    XSync(display, False);
}

void run() {
    XEvent event;

    XSync(display, False);
    fprintf(stderr, "Starting main event loop\n");

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
    SClient* client = clients;
    SClient* next;
    while (client) {
        next = client->next;
        free(client);
        client = next;
    }

    free(monitors);

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
    XButtonEvent* ev            = &event->xbutton;
    Window        clickedWindow = ev->window;

    fprintf(stderr, "ButtonPress: window=0x%lx, button=%d, state=0x%x\n", clickedWindow, ev->button, ev->state);

    if (clickedWindow == root) {
        Window       root_return, child_return;
        int          root_x, root_y, win_x, win_y;
        unsigned int mask_return;

        if (XQueryPointer(display, root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask_return) && child_return != None) {

            clickedWindow = child_return;
            fprintf(stderr, "  Found child: 0x%lx\n", clickedWindow);
        }
    }

    SClient* client = findClient(clickedWindow);
    fprintf(stderr, "  Client found: %s\n", client ? "yes" : "no");

    if (client) {
        if (ev->button == Button1 && ev->state == MODKEY) {
            fprintf(stderr, "  Starting window movement with mod+button1\n");

            windowMovement.client = client;
            windowMovement.x      = ev->x_root;
            windowMovement.y      = ev->y_root;
            windowMovement.active = 1;

            XChangeActivePointerGrab(display, ButtonReleaseMask | ButtonMotionMask, moveCursor, CurrentTime);
        } else if (ev->button == Button3 && ev->state == MODKEY) {
            fprintf(stderr, "  Starting window resize with mod+button3\n");

            windowResize.client = client;
            windowResize.x      = ev->x_root;
            windowResize.y      = ev->y_root;
            windowResize.active = 1;

            XChangeActivePointerGrab(display, ButtonReleaseMask | ButtonMotionMask, resizeCursor, CurrentTime);
        }
    } else if (ev->button == Button1 && clickedWindow != root)
        manageClient(clickedWindow);
}

void handleButtonRelease(XEvent* event) {
    XButtonEvent* ev = &event->xbutton;

    fprintf(stderr, "ButtonRelease: button=%d, state=0x%x\n", ev->button, ev->state);

    if (windowMovement.active && ev->button == Button1) {
        fprintf(stderr, "  Ending window movement\n");
        windowMovement.active = 0;
        windowMovement.client = NULL;
    }

    if (windowResize.active && ev->button == Button3) {
        fprintf(stderr, "  Ending window resize\n");
        windowResize.active = 0;
        windowResize.client = NULL;
    }
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
    }

    if (windowResize.active && windowResize.client) {
        int dx = ev->x_root - windowResize.x;
        int dy = ev->y_root - windowResize.y;

        int newWidth  = windowResize.client->width + dx;
        int newHeight = windowResize.client->height + dy;

        if (newWidth < 20)
            newWidth = 20;
        if (newHeight < 20)
            newHeight = 20;

        resizeWindow(windowResize.client, newWidth, newHeight);

        windowResize.x = ev->x_root;
        windowResize.y = ev->y_root;
    }
}

void moveWindow(SClient* client, int x, int y) {
    if (!client)
        return;

    client->x = x;
    client->y = y;

    XMoveWindow(display, client->window, client->x, client->y);
}

void resizeWindow(SClient* client, int width, int height) {
    if (!client)
        return;

    client->width  = width;
    client->height = height;

    XResizeWindow(display, client->window, client->width, client->height);

    configureClient(client);
}

void handleEnterNotify(XEvent* event) {
    XCrossingEvent* ev = &event->xcrossing;

    if (ev->mode != NotifyNormal) {
        fprintf(stderr, "EnterNotify: ignoring non-normal mode (%d)\n", ev->mode);
        return;
    }

    SClient* client = findClient(ev->window);
    if (!client) {
        fprintf(stderr, "EnterNotify: window 0x%lx not managed\n", ev->window);
        return;
    }

    if (focused != client) {
        fprintf(stderr, "EnterNotify: focusing window 0x%lx\n", ev->window);
        focusClient(client);
    } else
        fprintf(stderr, "EnterNotify: window 0x%lx already focused\n", ev->window);
}

void handleMapRequest(XEvent* event) {
    XMapRequestEvent* ev = &event->xmaprequest;

    XMapWindow(display, ev->window);

    XSync(display, False);

    manageClient(ev->window);
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
    XConfigureWindow(display, ev->window, ev->value_mask, &wc);
}

void handleUnmapNotify(XEvent* event) {
    XUnmapEvent* ev     = &event->xunmap;
    SClient*     client = findClient(ev->window);
    if (client)
        unmanageClient(ev->window);
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
    SClient* clientToKill = focused;

    if (arg && *arg) {
        XTextProperty windowName;
        SClient*      client = clients;

        while (client) {
            if (XGetWMName(display, client->window, &windowName) && windowName.value && strstr((char*)windowName.value, arg)) {
                clientToKill = client;
                XFree(windowName.value);
                break;
            }

            if (windowName.value)
                XFree(windowName.value);

            client = client->next;
        }
    }

    if (!clientToKill)
        return;

    XEvent event;
    event.type                 = ClientMessage;
    event.xclient.window       = clientToKill->window;
    event.xclient.message_type = WM_PROTOCOLS;
    event.xclient.format       = 32;
    event.xclient.data.l[0]    = WM_DELETE_WINDOW;
    event.xclient.data.l[1]    = CurrentTime;
    XSendEvent(display, clientToKill->window, False, NoEventMask, &event);

    XGrabServer(display);
    XKillClient(display, clientToKill->window);
    XUngrabServer(display);
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

    fprintf(stderr, "  Window is valid and viewable, setting focus\n");

    focused = client;

    XRaiseWindow(display, client->window);

    XSetInputFocus(display, client->window, RevertToPointerRoot, CurrentTime);

    updateBorders();
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

    client->window  = window;
    client->x       = wa.x;
    client->y       = wa.y;
    client->width   = wa.width;
    client->height  = wa.height;
    client->monitor = monitorAtPoint(wa.x + wa.width / 2, wa.y + wa.height / 2)->num;
    client->next    = clients;
    clients         = client;

    XSetWindowBorderWidth(display, window, BORDER_WIDTH);

    XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

    XSelectInput(display, window, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);

    XGrabButton(display, Button1, MODKEY, window, False, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, moveCursor);

    XGrabButton(display, Button3, MODKEY, window, False, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, resizeCursor);

    XSync(display, False);
    XSetErrorHandler(oldHandler);

    fprintf(stderr, "Client managed: 0x%lx\n", window);

    if (wa.map_state == IsViewable) {
        fprintf(stderr, "Window is viewable, focusing now\n");
        focusClient(client);
    } else
        fprintf(stderr, "Window not yet viewable (state: %d), deferring focus\n", wa.map_state);
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
        focused = (client->next) ? client->next : prev;
        updateFocus();
    }

    if (prev)
        prev->next = client->next;
    else
        clients = client->next;

    free(client);
}

void configureClient(SClient* client) {
    if (!client)
        return;

    XConfigureEvent event;
    event.type              = ConfigureNotify;
    event.display           = display;
    event.event             = client->window;
    event.window            = client->window;
    event.x                 = client->x;
    event.y                 = client->y;
    event.width             = client->width;
    event.height            = client->height;
    event.border_width      = BORDER_WIDTH;
    event.above             = None;
    event.override_redirect = False;
    XSendEvent(display, client->window, False, StructureNotifyMask, (XEvent*)&event);
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
    free(monitors);
    monitors    = NULL;
    numMonitors = 0;

    if (XineramaIsActive(display)) {
        XineramaScreenInfo* info = XineramaQueryScreens(display, &numMonitors);
        if (numMonitors > 0) {
            monitors = malloc(sizeof(SMonitor) * numMonitors);
            if (monitors) {
                for (int i = 0; i < numMonitors; i++) {
                    monitors[i].x      = info[i].x_org;
                    monitors[i].y      = info[i].y_org;
                    monitors[i].width  = info[i].width;
                    monitors[i].height = info[i].height;
                    monitors[i].num    = i;
                }
            }
            XFree(info);
        }
    }

    if (numMonitors == 0) {
        numMonitors = 1;
        monitors    = malloc(sizeof(SMonitor));
        if (monitors) {
            monitors[0].x      = 0;
            monitors[0].y      = 0;
            monitors[0].width  = DisplayWidth(display, DefaultScreen(display));
            monitors[0].height = DisplayHeight(display, DefaultScreen(display));
            monitors[0].num    = 0;
        }
    }
}

int main() {
    signal(SIGCHLD, SIG_IGN);

    setup();

    run();

    cleanup();

    return 0;
}
