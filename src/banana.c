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

Display* display;
Window root;
SClient* clients = NULL;
SClient* focused = NULL;
SMonitor* monitors = NULL;
int numMonitors = 0;
Cursor normalCursor;

Atom WM_PROTOCOLS;
Atom WM_DELETE_WINDOW;

int xerrorHandler(Display* dpy, XErrorEvent* ee) {
    if (ee->error_code == BadWindow ||
        (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
        (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
        (ee->request_code == X_GetGeometry && ee->error_code == BadDrawable)) {
        return 0;
    }

    char errorText[256];
    XGetErrorText(dpy, ee->error_code, errorText, sizeof(errorText));
    fprintf(stderr, "banana: X error: %s (0x%x) request %d\n",
            errorText, ee->error_code, ee->request_code);
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
    [KeyPress] = handleKeyPress,
    [ButtonPress] = handleButtonPress,
    [MotionNotify] = handleMotionNotify,
    [MapRequest] = handleMapRequest,
    [ConfigureRequest] = handleConfigureRequest,
    [UnmapNotify] = handleUnmapNotify,
    [DestroyNotify] = handleDestroyNotify,
};

void scanExistingWindows() {
    Window rootReturn, parentReturn;
    Window* children;
    unsigned int numChildren;

    if (XQueryTree(display, root, &rootReturn, &parentReturn, &children, &numChildren)) {
        for (unsigned int i = 0; i < numChildren; i++) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(display, children[i], &wa) &&
                !wa.override_redirect && wa.map_state == IsViewable) {
                manageClient(children[i]);
            }
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

    WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", False);
    WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);

    normalCursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, normalCursor);

    XSetWindowAttributes wa;
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                   ButtonPressMask | PointerMotionMask | EnterWindowMask |
                   LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(display, root, CWEventMask, &wa);
    XSelectInput(display, root, wa.event_mask);
    XSync(display, False);

    updateMonitors();
    grabKeys();

    scanExistingWindows();

    XSync(display, False);
}

void run() {
    XEvent event;
    while (!XNextEvent(display, &event)) {
        if (eventHandlers[event.type])
            eventHandlers[event.type](&event);
        XSync(display, False);
    }
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

    XCloseDisplay(display);
}

void handleKeyPress(XEvent* event) {
    XKeyEvent* ev = &event->xkey;
    KeySym keysym = XLookupKeysym(ev, 0);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].keysym == keysym && keys[i].mod == ev->state) {
            keys[i].func(keys[i].arg);
            break;
        }
    }
}

void handleButtonPress(XEvent* event) {
    XButtonEvent* ev = &event->xbutton;
    SClient* client = findClient(ev->window);
    if (client)
        focusClient(client);
}

void handleMotionNotify(XEvent* event) {
    if (!FOCUS_FOLLOWS_MOUSE)
        return;

    XMotionEvent* ev = &event->xmotion;
    SClient* client = clientAtPoint(ev->x_root, ev->y_root);
    if (client && client != focused)
        focusClient(client);
}

void handleMapRequest(XEvent* event) {
    XMapRequestEvent* ev = &event->xmaprequest;
    manageClient(ev->window);
    XMapWindow(display, ev->window);
}

void handleConfigureRequest(XEvent* event) {
    XConfigureRequestEvent* ev = &event->xconfigurerequest;
    XWindowChanges wc;
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(display, ev->window, ev->value_mask, &wc);
}

void handleUnmapNotify(XEvent* event) {
    XUnmapEvent* ev = &event->xunmap;
    SClient* client = findClient(ev->window);
    if (client)
        unmanageClient(ev->window);
}

void handleDestroyNotify(XEvent* event) {
    XDestroyWindowEvent* ev = &event->xdestroywindow;
    SClient* client = findClient(ev->window);
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
        SClient* client = clients;

        while (client) {
            if (XGetWMName(display, client->window, &windowName) &&
                windowName.value &&
                strstr((char*)windowName.value, arg)) {
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
    event.type = ClientMessage;
    event.xclient.window = clientToKill->window;
    event.xclient.message_type = WM_PROTOCOLS;
    event.xclient.format = 32;
    event.xclient.data.l[0] = WM_DELETE_WINDOW;
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, clientToKill->window, False, NoEventMask, &event);

    XGrabServer(display);
    XKillClient(display, clientToKill->window);
    XUngrabServer(display);
}

void quit(const char* arg) {
    int exitCode = EXIT_SUCCESS;

    if (arg && *arg) {
        char* endptr;
        long parsedCode = strtol(arg, &endptr, 10);

        if (*endptr == '\0' && parsedCode >= 0 && parsedCode <= 255)
            exitCode = (int)parsedCode;
    }

    cleanup();
    exit(exitCode);
}

void grabKeys() {
    XUngrabKey(display, AnyKey, AnyModifier, root);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        XGrabKey(display, XKeysymToKeycode(display, keys[i].keysym),
                keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
    }

    XUngrabButton(display, AnyButton, AnyModifier, root);

    XGrabButton(display, Button1, keys[0].mod, root, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, normalCursor);

    XGrabButton(display, Button3, keys[0].mod, root, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, normalCursor);
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
    if (!client || client == focused)
        return;
    focused = client;
    updateFocus();
}

void manageClient(Window window) {
    if (findClient(window))
        return;

    SClient* client = malloc(sizeof(SClient));
    if (!client)
        return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(display, window, &wa)) {
        free(client);
        return;
    }

    client->window = window;
    client->x = wa.x;
    client->y = wa.y;
    client->width = wa.width;
    client->height = wa.height;
    client->monitor = monitorAtPoint(wa.x + wa.width / 2, wa.y + wa.height / 2)->num;
    client->next = clients;
    clients = client;

    XSetWindowBorderWidth(display, window, BORDER_WIDTH);

    XSelectInput(display, window, EnterWindowMask | FocusChangeMask | PropertyChangeMask);

    focusClient(client);
    XSync(display, False);
}

void unmanageClient(Window window) {
    SClient* client = clients;
    SClient* prev = NULL;

    while (client && client->window != window) {
        prev = client;
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
    event.type = ConfigureNotify;
    event.display = display;
    event.event = client->window;
    event.window = client->window;
    event.x = client->x;
    event.y = client->y;
    event.width = client->width;
    event.height = client->height;
    event.border_width = BORDER_WIDTH;
    event.above = None;
    event.override_redirect = False;
    XSendEvent(display, client->window, False, StructureNotifyMask, (XEvent*)&event);
}

void updateBorders() {
    SClient* client = clients;
    unsigned long activeBorder, inactiveBorder;

    XColor color;
    Colormap cmap = DefaultColormap(display, DefaultScreen(display));

    XAllocNamedColor(display, cmap, ACTIVE_BORDER_COLOR, &color, &color);
    activeBorder = color.pixel;

    XAllocNamedColor(display, cmap, INACTIVE_BORDER_COLOR, &color, &color);
    inactiveBorder = color.pixel;

    while (client) {
        XSetWindowBorder(display, client->window,
                         (client == focused) ? activeBorder : inactiveBorder);
        client = client->next;
    }

    XSync(display, False);
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

SClient* clientAtPoint(int x, int y) {
    Window root_return, child_return;
    int win_x_return, win_y_return;
    unsigned int mask_return;

    if (XTranslateCoordinates(display, root, root, x, y, &win_x_return, &win_y_return, &child_return))
        return findClient(child_return);
    return NULL;
}

SMonitor* monitorAtPoint(int x, int y) {
    if (numMonitors <= 1)
        return &monitors[0];

    for (int i = 0; i < numMonitors; i++) {
        if (x >= monitors[i].x && x < monitors[i].x + monitors[i].width &&
            y >= monitors[i].y && y < monitors[i].y + monitors[i].height) {
            return &monitors[i];
        }
    }

    return &monitors[0];
}

void updateMonitors() {
    free(monitors);
    monitors = NULL;
    numMonitors = 0;

    if (XineramaIsActive(display)) {
        XineramaScreenInfo* info = XineramaQueryScreens(display, &numMonitors);
        if (numMonitors > 0) {
            monitors = malloc(sizeof(SMonitor) * numMonitors);
            if (monitors) {
                for (int i = 0; i < numMonitors; i++) {
                    monitors[i].x = info[i].x_org;
                    monitors[i].y = info[i].y_org;
                    monitors[i].width = info[i].width;
                    monitors[i].height = info[i].height;
                    monitors[i].num = i;
                }
            }
            XFree(info);
        }
    }

    if (numMonitors == 0) {
        numMonitors = 1;
        monitors = malloc(sizeof(SMonitor));
        if (monitors) {
            monitors[0].x = 0;
            monitors[0].y = 0;
            monitors[0].width = DisplayWidth(display, DefaultScreen(display));
            monitors[0].height = DisplayHeight(display, DefaultScreen(display));
            monitors[0].num = 0;
        }
    }
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("banana - a simple X11 window manager\n\n");
            printf("Usage: banana [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  -h, --help     Display this help and exit\n");
            printf("  -v, --version  Display version information and exit\n");
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("banana version 0.1.0\n");
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "banana: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Try 'banana --help' for more information.\n");
            return EXIT_FAILURE;
        }
    }

    signal(SIGCHLD, SIG_IGN);

    setup();

    run();

    cleanup();

    return 0;
}
