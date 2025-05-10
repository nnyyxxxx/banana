#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xrandr.h>
#include <sys/time.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include "banana.h"
#include "config.h"
#include "bar.h"
#include "ipc.h"

Display	       *display;
Window		root;
SClient	       *clients	    = NULL;
SClient	       *focused	    = NULL;
SMonitor       *monitors    = NULL;
int		numMonitors = 0;
Cursor		normalCursor;
Cursor		moveCursor;
Cursor		resizeSECursor;
Cursor		resizeSWCursor;
Cursor		resizeNECursor;
Cursor		resizeNWCursor;
SWindowMovement windowMovement	    = {0, 0, NULL, 0, 0};
SWindowResize	windowResize	    = {0, 0, NULL, 0, 0};
int		currentWorkspace    = 0;
Window		lastMappedWindow    = 0;
struct timeval	lastWindowOperation = {0, 0};
SClient	       *lastFocused	    = NULL;
int		lastCursorWarp	    = 0;

Atom		WM_PROTOCOLS;
Atom		WM_DELETE_WINDOW;
Atom		WM_STATE;
Atom		WM_TAKE_FOCUS;

Atom		NET_SUPPORTED;
Atom		NET_WM_NAME;
Atom		NET_SUPPORTING_WM_CHECK;
Atom		NET_CLIENT_LIST;
Atom		NET_NUMBER_OF_DESKTOPS;
Atom		NET_CURRENT_DESKTOP;
Atom		NET_DESKTOP_VIEWPORT;
Atom		NET_WM_STATE;
Atom		NET_WM_STATE_FULLSCREEN;
Atom		NET_WM_STATE_DEMANDS_ATTENTION;
Atom		NET_WM_WINDOW_TYPE;
Atom		NET_WM_WINDOW_TYPE_DIALOG;
Atom		NET_WM_WINDOW_TYPE_UTILITY;
Atom		NET_WM_WINDOW_TYPE_DOCK;
Atom		NET_ACTIVE_WINDOW;
Atom		NET_WM_STRUT;
Atom		NET_WM_STRUT_PARTIAL;
Atom		NET_WM_DESKTOP;
Atom		UTF8_STRING;
Window		wmcheckwin;

Atom		NET_CLIENT_LIST_STACKING;
Atom		NET_DESKTOP_NAMES;
Atom		NET_CLOSE_WINDOW;
Atom		NET_MOVERESIZE_WINDOW;
Atom		NET_WM_MOVERESIZE;
Atom		NET_REQUEST_FRAME_EXTENTS;
Atom		NET_FRAME_EXTENTS;
Atom		NET_WM_ALLOWED_ACTIONS;

Atom		NET_WM_ACTION_CLOSE;
Atom		NET_WM_ACTION_MAXIMIZE_HORZ;
Atom		NET_WM_ACTION_MAXIMIZE_VERT;
Atom		NET_WM_ACTION_FULLSCREEN;
Atom		NET_WM_ACTION_CHANGE_DESKTOP;
Atom		NET_WM_ACTION_MOVE;
Atom		NET_WM_ACTION_RESIZE;

int		xerrorHandler(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow ||
	    (ee->request_code == X_SetInputFocus &&
	     ee->error_code == BadMatch) ||
	    (ee->request_code == X_ConfigureWindow &&
	     ee->error_code == BadMatch) ||
	    (ee->request_code == X_GetGeometry &&
	     ee->error_code == BadDrawable)) {
		return 0;
	}

	char errorText[256];
	XGetErrorText(dpy, ee->error_code, errorText, sizeof(errorText));
	fprintf(stderr, "banana: X error: %s (0x%x) request %d\n", errorText,
		ee->error_code, ee->request_code);
	return 0;
}

int otherWmRunningHandler(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadAccess &&
	    ee->request_code == X_ChangeWindowAttributes) {
		fprintf(stderr, "banana: another window manager is already "
				"running\n");
		exit(1);
	}
	return xerrorHandler(dpy, ee);
}

void checkOtherWM()
{
	XSetErrorHandler(otherWmRunningHandler);
	XSelectInput(display, root, SubstructureRedirectMask);
	XSync(display, False);
	XSetErrorHandler(xerrorHandler);
	XSelectInput(display, root, 0);
	XSync(display, False);
}

static int rr_event_base;

static void (*eventHandlers[LASTEvent])(XEvent *) = {
    [KeyPress]	       = handleKeyPress,
    [ButtonPress]      = handleButtonPress,
    [ButtonRelease]    = handleButtonRelease,
    [MotionNotify]     = handleMotionNotify,
    [EnterNotify]      = handleEnterNotify,
    [MapRequest]       = handleMapRequest,
    [ConfigureRequest] = handleConfigureRequest,
    [UnmapNotify]      = handleUnmapNotify,
    [DestroyNotify]    = handleDestroyNotify,
    [Expose]	       = handleExpose,
    [PropertyNotify]   = handlePropertyNotify,
    [ClientMessage]    = handleClientMessage,
};

void scanExistingWindows()
{
	Window	     rootReturn, parentReturn;
	Window	    *children;
	unsigned int numChildren;

	if (XQueryTree(display, root, &rootReturn, &parentReturn, &children,
		       &numChildren)) {
		for (unsigned int i = 0; i < numChildren; i++) {
			XWindowAttributes wa;
			if (XGetWindowAttributes(display, children[i], &wa) &&
			    !wa.override_redirect &&
			    wa.map_state == IsViewable) {
				manageClient(children[i]);
			}
		}

		if (children) {
			XFree(children);
		}
	}
}

void setupEWMH()
{
	NET_SUPPORTED = XInternAtom(display, "_NET_SUPPORTED", False);
	NET_WM_NAME   = XInternAtom(display, "_NET_WM_NAME", False);
	NET_SUPPORTING_WM_CHECK =
	    XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
	NET_CLIENT_LIST = XInternAtom(display, "_NET_CLIENT_LIST", False);
	NET_NUMBER_OF_DESKTOPS =
	    XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
	NET_CURRENT_DESKTOP =
	    XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
	NET_DESKTOP_VIEWPORT =
	    XInternAtom(display, "_NET_DESKTOP_VIEWPORT", False);
	NET_WM_STATE = XInternAtom(display, "_NET_WM_STATE", False);
	NET_WM_STATE_FULLSCREEN =
	    XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
	NET_WM_STATE_DEMANDS_ATTENTION =
	    XInternAtom(display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
	NET_WM_WINDOW_TYPE = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
	NET_WM_WINDOW_TYPE_DIALOG =
	    XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	NET_WM_WINDOW_TYPE_UTILITY =
	    XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
	NET_WM_WINDOW_TYPE_DOCK =
	    XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
	NET_ACTIVE_WINDOW = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
	NET_WM_STRUT	  = XInternAtom(display, "_NET_WM_STRUT", False);
	NET_WM_STRUT_PARTIAL =
	    XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
	NET_WM_DESKTOP = XInternAtom(display, "_NET_WM_DESKTOP", False);
	UTF8_STRING    = XInternAtom(display, "UTF8_STRING", False);
	NET_CLIENT_LIST_STACKING =
	    XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
	NET_DESKTOP_NAMES = XInternAtom(display, "_NET_DESKTOP_NAMES", False);
	NET_CLOSE_WINDOW  = XInternAtom(display, "_NET_CLOSE_WINDOW", False);
	NET_MOVERESIZE_WINDOW =
	    XInternAtom(display, "_NET_MOVERESIZE_WINDOW", False);
	NET_WM_MOVERESIZE = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
	NET_REQUEST_FRAME_EXTENTS =
	    XInternAtom(display, "_NET_REQUEST_FRAME_EXTENTS", False);
	NET_FRAME_EXTENTS = XInternAtom(display, "_NET_FRAME_EXTENTS", False);
	NET_WM_ALLOWED_ACTIONS =
	    XInternAtom(display, "_NET_WM_ALLOWED_ACTIONS", False);
	NET_WM_ACTION_CLOSE =
	    XInternAtom(display, "_NET_WM_ACTION_CLOSE", False);
	NET_WM_ACTION_MAXIMIZE_HORZ =
	    XInternAtom(display, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
	NET_WM_ACTION_MAXIMIZE_VERT =
	    XInternAtom(display, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
	NET_WM_ACTION_FULLSCREEN =
	    XInternAtom(display, "_NET_WM_ACTION_FULLSCREEN", False);
	NET_WM_ACTION_CHANGE_DESKTOP =
	    XInternAtom(display, "_NET_WM_ACTION_CHANGE_DESKTOP", False);
	NET_WM_ACTION_MOVE = XInternAtom(display, "_NET_WM_ACTION_MOVE", False);
	NET_WM_ACTION_RESIZE =
	    XInternAtom(display, "_NET_WM_ACTION_RESIZE", False);

	wmcheckwin = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(display, root, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)&wmcheckwin, 1);
	XChangeProperty(display, wmcheckwin, NET_SUPPORTING_WM_CHECK, XA_WINDOW,
			32, PropModeReplace, (unsigned char *)&wmcheckwin, 1);
	XChangeProperty(display, wmcheckwin, NET_WM_NAME, UTF8_STRING, 8,
			PropModeReplace, (unsigned char *)"banana", 6);

	Atom supported[] = {NET_SUPPORTED,
			    NET_WM_NAME,
			    NET_SUPPORTING_WM_CHECK,
			    NET_CLIENT_LIST,
			    NET_NUMBER_OF_DESKTOPS,
			    NET_CURRENT_DESKTOP,
			    NET_DESKTOP_VIEWPORT,
			    NET_WM_STATE,
			    NET_WM_STATE_FULLSCREEN,
			    NET_WM_STATE_DEMANDS_ATTENTION,
			    NET_WM_WINDOW_TYPE,
			    NET_WM_WINDOW_TYPE_DIALOG,
			    NET_WM_WINDOW_TYPE_UTILITY,
			    NET_WM_WINDOW_TYPE_DOCK,
			    NET_ACTIVE_WINDOW,
			    NET_WM_STRUT,
			    NET_WM_STRUT_PARTIAL,
			    NET_WM_DESKTOP,
			    NET_CLIENT_LIST_STACKING,
			    NET_DESKTOP_NAMES,
			    NET_CLOSE_WINDOW,
			    NET_MOVERESIZE_WINDOW,
			    NET_WM_MOVERESIZE,
			    NET_REQUEST_FRAME_EXTENTS,
			    NET_FRAME_EXTENTS,
			    NET_WM_ALLOWED_ACTIONS};

	XChangeProperty(display, root, NET_SUPPORTED, XA_ATOM, 32,
			PropModeReplace, (unsigned char *)supported,
			sizeof(supported) / sizeof(Atom));

	long numDesktops = workspaceCount;
	XChangeProperty(display, root, NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&numDesktops, 1);

	long currentDesktop = currentWorkspace;
	XChangeProperty(display, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&currentDesktop, 1);

	updateClientList();
	updateClientListStacking();
	updateDesktopViewport();
	updateDesktopNames();
}

void setup()
{
	display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "banana: cannot open display\n");
		exit(1);
	}

	XSetErrorHandler(xerrorHandler);
	root = DefaultRootWindow(display);

	checkOtherWM();

	WM_PROTOCOLS	 = XInternAtom(display, "WM_PROTOCOLS", False);
	WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);
	WM_STATE	 = XInternAtom(display, "WM_STATE", False);
	WM_TAKE_FOCUS	 = XInternAtom(display, "WM_TAKE_FOCUS", False);

	setupEWMH();

	if (!loadConfig()) {
		fprintf(stderr, "banana: failed to load configuration\n");
		exit(1);
	}

	int rr_error_base;
	if (!XRRQueryExtension(display, &rr_event_base, &rr_error_base)) {
		fprintf(stderr, "banana: RandR extension not available\n");
	} else {
		XRRSelectInput(display, root, RRScreenChangeNotifyMask);
		fprintf(stderr, "RandR extension initialized, listening for "
				"screen changes\n");
	}

	normalCursor   = XcursorLibraryLoadCursor(display, "left_ptr");
	moveCursor     = XcursorLibraryLoadCursor(display, "fleur");
	resizeSECursor = XcursorLibraryLoadCursor(display, "se-resize");
	resizeSWCursor = XcursorLibraryLoadCursor(display, "sw-resize");
	resizeNECursor = XcursorLibraryLoadCursor(display, "ne-resize");
	resizeNWCursor = XcursorLibraryLoadCursor(display, "nw-resize");
	XDefineCursor(display, root, normalCursor);

	XSetWindowAttributes wa;
	wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
			ButtonPressMask | ButtonReleaseMask | EnterWindowMask |
			LeaveWindowMask | PointerMotionMask | ButtonMotionMask |
			StructureNotifyMask | PropertyChangeMask;
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

	for (int i = 0; i < numMonitors; i++) {
		if (hasDocksOnMonitor(i) && barVisible) {
			fprintf(stderr,
				"Hiding bar because docks are detected on "
				"monitor %d\n",
				i);
			barVisible = 0;
			showHideBars(0);
			break;
		}
	}

	scanExistingWindows();

	updateClientPositionsForBar();
	updateClientVisibility();

	if (ipcInitServer() != 0) {
		fprintf(stderr, "Failed to initialize IPC server\n");
	}

	XSync(display, False);
}

void checkCursorPosition(struct timeval *lastCheck, int *lastCursorX,
			 int *lastCursorY, Window *lastWindow)
{
	if (windowMovement.active || windowResize.active) {
		return;
	}
	struct timeval now;
	gettimeofday(&now, NULL);

	int elapsed_ms = ((now.tv_sec - lastCheck->tv_sec) * 1000) +
			 ((now.tv_usec - lastCheck->tv_usec) / 1000);

	if (elapsed_ms <= 50) {
		return;
	}

	int time_since_window_op =
	    ((now.tv_sec - lastWindowOperation.tv_sec) * 1000) +
	    ((now.tv_usec - lastWindowOperation.tv_usec) / 1000);
	int window_op_cooldown = (time_since_window_op < 200);

	memcpy(lastCheck, &now, sizeof(struct timeval));

	Window	     root_return, child_return;
	int	     root_x, root_y, win_x, win_y;
	unsigned int mask;

	if (!XQueryPointer(display, root, &root_return, &child_return, &root_x,
			   &root_y, &win_x, &win_y, &mask)) {
		return;
	}

	int cursor_moved =
	    (abs(root_x - *lastCursorX) > 5 || abs(root_y - *lastCursorY) > 5);

	if (!no_warps && lastCursorWarp && !cursor_moved) {
		*lastCursorX = root_x;
		*lastCursorY = root_y;
		*lastWindow  = child_return;
		return;
	}

	if (cursor_moved) {
		lastCursorWarp = 0;
	}

	if (root_x == *lastCursorX && root_y == *lastCursorY &&
	    child_return == *lastWindow) {
		return;
	}

	*lastCursorX = root_x;
	*lastCursorY = root_y;
	*lastWindow  = child_return;

	SMonitor *currentMonitor = monitorAtPoint(root_x, root_y);

	int	  activeMonitor = -1;
	if (focused) {
		activeMonitor = focused->monitor;
	}

	SClient *windowUnderCursor = NULL;
	if (child_return != None && child_return != root) {
		windowUnderCursor = findClient(child_return);
	}

	if (windowUnderCursor && !window_op_cooldown) {
		if (windowUnderCursor == focused) {
			return;
		}

		SMonitor *monitor = &monitors[windowUnderCursor->monitor];
		if (windowUnderCursor->workspace != monitor->currentWorkspace) {
			return;
		}

		fprintf(stderr,
			"Cursor over window 0x%lx (currently focused: 0x%lx)\n",
			windowUnderCursor->window,
			focused ? focused->window : 0);
		focusClient(windowUnderCursor);
		return;
	}

	if (activeMonitor != -1 && currentMonitor->num != activeMonitor) {
		SClient *clientInWorkspace = findVisibleClientInWorkspace(
		    currentMonitor->num, currentMonitor->currentWorkspace);

		if (clientInWorkspace) {
			fprintf(stderr, "Focusing client on monitor %d\n",
				currentMonitor->num);
			focusClient(clientInWorkspace);
		} else {
			fprintf(stderr, "Focusing root on monitor %d\n",
				currentMonitor->num);
			focused = NULL;
			XSetInputFocus(display, root, RevertToPointerRoot,
				       CurrentTime);
			updateBorders();
			updateBars();
		}
	}
}

void run()
{
	XEvent	       event;
	struct timeval lastCheck;
	int	       lastCursorX = 0, lastCursorY = 0;
	Window	       lastWindow = None;
	gettimeofday(&lastCheck, NULL);

	XSync(display, False);
	fprintf(stderr, "Starting main event loop\n");

	updateClientVisibility();
	updateBars();

	while (1) {
		if (XPending(display)) {
			XNextEvent(display, &event);

			if (event.type ==
			    rr_event_base + RRScreenChangeNotify) {
				handleScreenChange(&event);
			} else if (eventHandlers[event.type]) {
				XErrorHandler oldHandler =
				    XSetErrorHandler(xerrorHandler);
				eventHandlers[event.type](&event);
				XSync(display, False);
				XSetErrorHandler(oldHandler);
			}
		} else {
			int ipc_result = ipcHandleCommands();
			if (ipc_result > 0) {
				reloadConfig(NULL);
			}

			checkCursorPosition(&lastCheck, &lastCursorX,
					    &lastCursorY, &lastWindow);
			usleep(5000);
		}
	}
}

void cleanup()
{
	if (barWindows) {
		for (int i = 0; i < numMonitors; i++) {
			if (barWindows[i] != 0) {
				XDestroyWindow(display, barWindows[i]);
			}
		}
		free(barWindows);
	}

	if (monitors) {
		for (int i = 0; i < numMonitors; i++) {
			free(monitors[i].masterFactors);
			free(monitors[i].workspaceLayouts);
			free(monitors[i].lastTiledClient);
		}
		free(monitors);
	}

	while (clients) {
		SClient *tmp = clients;
		clients	     = clients->next;
		free(tmp);
	}

	if (wmcheckwin) {
		XDestroyWindow(display, wmcheckwin);
	}

	XFreeCursor(display, normalCursor);
	XFreeCursor(display, moveCursor);
	XFreeCursor(display, resizeSECursor);
	XFreeCursor(display, resizeSWCursor);
	XFreeCursor(display, resizeNECursor);
	XFreeCursor(display, resizeNWCursor);

	freeConfig();

	ipcCleanup();

	XCloseDisplay(display);
}

void handleKeyPress(XEvent *event)
{
	XKeyEvent   *ev	    = &event->xkey;
	KeySym	     keysym = XLookupKeysym(ev, 0);

	unsigned int state = ev->state & ~LockMask;

	lastMappedWindow = 0;

	for (size_t i = 0; i < keysCount; i++) {
		if (keys[i].keysym == keysym && keys[i].mod == state) {
			keys[i].func(keys[i].arg);
			break;
		}
	}
}

void handleButtonPress(XEvent *event)
{
	XButtonPressedEvent *ev		   = &event->xbutton;
	Window		     clickedWindow = ev->window;

	lastMappedWindow = 0;

	for (int i = 0; i < numMonitors; i++) {
		if (barWindows && barWindows[i] == clickedWindow) {
			handleBarClick(event);
			return;
		}
	}

	SClient *client = findClient(ev->window);

	if (!client || (ev->state & modkey) == 0) {
		return;
	}

	if (ev->button == Button1) {
		if (!client->isFloating) {
			client->isFloating	= 1;
			windowMovement.wasTiled = 1;

			SMonitor *monitor = &monitors[client->monitor];

			int	  oldWidth  = client->width;
			int	  oldHeight = client->height;

			int	  newWidth  = oldWidth * 0.8;
			int	  newHeight = oldHeight * 0.8;

			newWidth  = MAX(20, newWidth);
			newHeight = MAX(10, newHeight);

			if (client->sizeHints.valid) {
				if (client->sizeHints.minWidth > 0 &&
				    newWidth < client->sizeHints.minWidth) {
					newWidth = client->sizeHints.minWidth;
				}
				if (client->sizeHints.minHeight > 0 &&
				    newHeight < client->sizeHints.minHeight) {
					newHeight = client->sizeHints.minHeight;
				}
			}

			int newX = ev->x_root - newWidth / 2;
			int newY = ev->y_root - newHeight / 2;

			client->x      = newX;
			client->y      = newY;
			client->width  = newWidth;
			client->height = newHeight;

			XSetWindowBorderWidth(display, client->window,
					      borderWidth);
			updateBorders();

			XMoveResizeWindow(display, client->window, client->x,
					  client->y, client->width,
					  client->height);
			XRaiseWindow(display, client->window);

			XGrabButton(display, Button3, modkey, client->window,
				    False,
				    ButtonPressMask | ButtonReleaseMask |
					ButtonMotionMask,
				    GrabModeAsync, GrabModeAsync, None,
				    resizeSECursor);

			windowMovement.client = client;
			windowMovement.x      = ev->x_root;
			windowMovement.y      = ev->y_root;
			windowMovement.active = 1;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     moveCursor, CurrentTime);

			arrangeClients(monitor);
		} else {
			windowMovement.client	= client;
			windowMovement.x	= ev->x_root;
			windowMovement.y	= ev->y_root;
			windowMovement.active	= 1;
			windowMovement.wasTiled = 0;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     moveCursor, CurrentTime);
		}
	} else if (ev->button == Button3 && client->isFloating) {
		windowResize.client = client;
		windowResize.x	    = ev->x_root;
		windowResize.y	    = ev->y_root;
		windowResize.active = 1;

		int relX = ev->x_root - client->x;
		int relY = ev->y_root - client->y;

		int cornerWidth	 = client->width * 0.5;
		int cornerHeight = client->height * 0.5;

		if (relX < cornerWidth && relY < cornerHeight) {
			windowResize.resizeType = 3;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     resizeNWCursor, CurrentTime);
		} else if (relX > client->width - cornerWidth &&
			   relY < cornerHeight) {
			windowResize.resizeType = 2;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     resizeNECursor, CurrentTime);
		} else if (relX < cornerWidth &&
			   relY > client->height - cornerHeight) {
			windowResize.resizeType = 1;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     resizeSWCursor, CurrentTime);
		} else if (relX > client->width - cornerWidth &&
			   relY > client->height - cornerHeight) {
			windowResize.resizeType = 0;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     resizeSECursor, CurrentTime);
		} else {
			windowResize.resizeType = 0;
			XGrabPointer(display, root, False, MOUSEMASK,
				     GrabModeAsync, GrabModeAsync, None,
				     resizeSECursor, CurrentTime);
		}
	}
}

void swapWindowUnderCursor(SClient *client, int cursorX, int cursorY)
{
	if (!client || !client->isFloating || !windowMovement.wasTiled) {
		return;
	}

	SClient *targetClient = NULL;

	for (SClient *c = clients; c; c = c->next) {
		if (c != client && c->monitor == client->monitor &&
		    c->workspace == client->workspace && !c->isFloating &&
		    !c->isFullscreen && cursorX >= c->x &&
		    cursorX < c->x + c->width && cursorY >= c->y &&
		    cursorY < c->y + c->height) {
			targetClient = c;
			break;
		}
	}

	if (!targetClient) {
		int closestDistance = INT_MAX;

		for (SClient *c = clients; c; c = c->next) {
			if (c != client && c->monitor == client->monitor &&
			    c->workspace == client->workspace &&
			    !c->isFloating && !c->isFullscreen) {
				int centerX = c->x + c->width / 2;
				int centerY = c->y + c->height / 2;
				int distance =
				    (cursorX - centerX) * (cursorX - centerX) +
				    (cursorY - centerY) * (cursorY - centerY);

				if (distance < closestDistance) {
					closestDistance = distance;
					targetClient	= c;
				}
			}
		}
	}

	if (targetClient) {
		fprintf(stderr, "Swapping client 0x%lx with 0x%lx\n",
			client->window, targetClient->window);
		client->isFloating = 0;

		XUngrabButton(display, Button3, modkey, client->window);

		SMonitor *monitor = &monitors[client->monitor];
		if (monitor->currentLayout == LAYOUT_MONOCLE) {
			Window lastVisible =
			    monitor->lastTiledClient[client->workspace];
			SClient *lastClient = findClient(lastVisible);

			if (lastClient && lastClient != targetClient &&
			    lastClient != client) {
				fprintf(stderr,
					"Unmapping previously visible monocle "
					"window: 0x%lx\n",
					lastClient->window);
				XUnmapWindow(display, lastClient->window);
			}

			monitor->lastTiledClient[client->workspace] =
			    client->window;
		}

		swapClients(client, targetClient);
		arrangeClients(&monitors[client->monitor]);
	} else {
		client->isFloating = 0;

		XUngrabButton(display, Button3, modkey, client->window);

		arrangeClients(&monitors[client->monitor]);
	}

	restackFloatingWindows();
}

void handleButtonRelease(XEvent *event)
{
	XButtonEvent *ev = &event->xbutton;

	if (windowMovement.active && ev->button == Button1) {
		SClient *movingClient = windowMovement.client;

		if (movingClient && windowMovement.wasTiled) {
			fprintf(stderr,
				"Attempting to swap with window under cursor "
				"at %d,%d\n",
				ev->x_root, ev->y_root);
			swapWindowUnderCursor(movingClient, ev->x_root,
					      ev->y_root);
		} else if (movingClient) {
			moveWindow(movingClient, movingClient->x,
				   movingClient->y);

			focusClient(movingClient);
		}

		windowMovement.active	= 0;
		windowMovement.client	= NULL;
		windowMovement.wasTiled = 0;
		XUngrabPointer(display, CurrentTime);

		updateBars();
	}

	if (windowResize.active && ev->button == Button3) {
		SClient *resizingClient = windowResize.client;

		if (resizingClient) {
			focusClient(resizingClient);
		}

		windowResize.active = 0;
		windowResize.client = NULL;
		XUngrabPointer(display, CurrentTime);

		updateBars();
	}
}

SClient *clientAtPoint(int x, int y)
{
	SClient *client = clients;

	while (client) {
		if (x >= client->x && x < client->x + client->width &&
		    y >= client->y && y < client->y + client->height) {
			return client;
		}
		client = client->next;
	}

	return NULL;
}

void handleMotionNotify(XEvent *event)
{
	XMotionEvent *ev	     = &event->xmotion;
	static int    lastMonitor    = -1;
	SMonitor     *currentMonitor = monitorAtPoint(ev->x_root, ev->y_root);

	if (no_warps) {
		forcedMonitor = currentMonitor->num;
	}

	lastCursorWarp = 0;

	if (lastMonitor != currentMonitor->num) {
		lastMonitor = currentMonitor->num;
		updateBars();

		currentWorkspace = currentMonitor->currentWorkspace;

		if ((windowResize.active || windowMovement.active) && focused) {
			return;
		}

		if (!focused || focused->monitor != currentMonitor->num) {
			SClient *clientInWorkspace =
			    findVisibleClientInWorkspace(
				currentMonitor->num,
				currentMonitor->currentWorkspace);

			if (clientInWorkspace) {
				focusClient(clientInWorkspace);
			} else {
				XSetInputFocus(display, root,
					       RevertToPointerRoot,
					       CurrentTime);
				focused = NULL;
				updateBorders();
			}
		}
	}

	while (XCheckTypedWindowEvent(display, ev->window, MotionNotify, event))
		;

	if (windowMovement.active && windowMovement.client) {
		int dx = ev->x_root - windowMovement.x;
		int dy = ev->y_root - windowMovement.y;

		moveWindow(windowMovement.client, windowMovement.client->x + dx,
			   windowMovement.client->y + dy);

		windowMovement.x = ev->x_root;
		windowMovement.y = ev->y_root;
	} else if (windowResize.active && windowResize.client) {
		int	 dx	= ev->x_root - windowResize.x;
		int	 dy	= ev->y_root - windowResize.y;
		SClient *client = windowResize.client;

		if (client->isFullscreen) {
			windowResize.x = ev->x_root;
			windowResize.y = ev->y_root;
			return;
		}

		if (!client->isFloating) {
			windowResize.x = ev->x_root;
			windowResize.y = ev->y_root;
			return;
		}

		int isFixedSize =
		    (client->sizeHints.valid && client->sizeHints.maxWidth &&
		     client->sizeHints.maxHeight &&
		     client->sizeHints.minWidth &&
		     client->sizeHints.minHeight &&
		     client->sizeHints.maxWidth == client->sizeHints.minWidth &&
		     client->sizeHints.maxHeight ==
			 client->sizeHints.minHeight);

		Atom windowType = getAtomProperty(client, NET_WM_WINDOW_TYPE);
		if (isFixedSize || windowType == NET_WM_WINDOW_TYPE_UTILITY) {
			windowResize.x = ev->x_root;
			windowResize.y = ev->y_root;
			return;
		}

		int newWidth, newHeight, newX, newY;

		switch (windowResize.resizeType) {
		case 1:
			newWidth  = client->width - dx;
			newHeight = client->height + dy;
			newX	  = client->x + dx;
			newY	  = client->y;

			if (newWidth >= 15) {
				client->x     = newX;
				client->width = newWidth;
			}
			if (newHeight >= 15) {
				client->height = newHeight;
			}
			break;

		case 2:
			newWidth  = client->width + dx;
			newHeight = client->height - dy;
			newX	  = client->x;
			newY	  = client->y + dy;

			if (newWidth >= 15) {
				client->width = newWidth;
			}
			if (newHeight >= 15) {
				client->y      = newY;
				client->height = newHeight;
			}
			break;

		case 3:
			newWidth  = client->width - dx;
			newHeight = client->height - dy;
			newX	  = client->x + dx;
			newY	  = client->y + dy;

			if (newWidth >= 15) {
				client->x     = newX;
				client->width = newWidth;
			}
			if (newHeight >= 15) {
				client->y      = newY;
				client->height = newHeight;
			}
			break;

		case 0:
		default:
			resizeWindow(windowResize.client,
				     windowResize.client->width + dx,
				     windowResize.client->height + dy);
			break;
		}

		if (windowResize.resizeType != 0) {
			if (client->sizeHints.valid) {
				if (client->sizeHints.minWidth > 0 &&
				    client->width <
					client->sizeHints.minWidth) {
					int oldWidth = client->width;
					client->width =
					    client->sizeHints.minWidth;
					if (windowResize.resizeType == 1 ||
					    windowResize.resizeType == 3) {
						client->x -=
						    (client->width - oldWidth);
					}
				}

				if (client->sizeHints.minHeight > 0 &&
				    client->height <
					client->sizeHints.minHeight) {
					int oldHeight = client->height;
					client->height =
					    client->sizeHints.minHeight;
					if (windowResize.resizeType == 2 ||
					    windowResize.resizeType == 3) {
						client->y -= (client->height -
							      oldHeight);
					}
				}

				if (client->sizeHints.maxWidth > 0 &&
				    client->width >
					client->sizeHints.maxWidth) {
					client->width =
					    client->sizeHints.maxWidth;
				}

				if (client->sizeHints.maxHeight > 0 &&
				    client->height >
					client->sizeHints.maxHeight) {
					client->height =
					    client->sizeHints.maxHeight;
				}
			}

			XMoveResizeWindow(display, client->window, client->x,
					  client->y, client->width,
					  client->height);
			XRaiseWindow(display, client->window);
			configureClient(client);
		}

		windowResize.x = ev->x_root;
		windowResize.y = ev->y_root;
	}
}

void moveWindow(SClient *client, int x, int y)
{
	if (!client) {
		return;
	}

	if (!client->isFloating) {
		return;
	}

	if (client->isFullscreen) {
		return;
	}

	if (client->isDock) {
		return;
	}

	int prevMonitor = client->monitor;

	client->x = x;
	client->y = y;

	SMonitor *monitor = &monitors[client->monitor];
	int	  centerX = client->x + client->width / 2;
	int	  centerY = client->y + client->height / 2;
	monitor		  = monitorAtPoint(centerX, centerY);
	client->monitor	  = monitor->num;

	if (prevMonitor != client->monitor) {
		client->workspace = monitor->currentWorkspace;
	}

	XMoveWindow(display, client->window, client->x, client->y);

	configureClient(client);

	if (prevMonitor != client->monitor) {
		fprintf(stderr, "Window moved to a different monitor, updating "
				"layout\n");

		focusClient(client);

		arrangeClients(&monitors[prevMonitor]);
		arrangeClients(monitor);

		updateBars();
	}

	if (windowMovement.active && windowMovement.client == client) {
		XRaiseWindow(display, client->window);
	}
}

void resizeWindow(SClient *client, int width, int height)
{
	if (!client) {
		return;
	}

	if (!client->isFloating) {
		return;
	}
	if (client->isFullscreen) {
		SMonitor *monitor = &monitors[client->monitor];
		client->x	  = monitor->x;
		client->y	  = monitor->y;
		client->width	  = monitor->width;
		client->height	  = monitor->height;

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);
		return;
	}

	if (client->isDock) {
		return;
	}

	if (width < 15) {
		width = 15;
	}
	if (height < 15) {
		height = 15;
	}

	if (client->sizeHints.valid) {
		if (client->sizeHints.minWidth > 0 &&
		    width < client->sizeHints.minWidth) {
			width = client->sizeHints.minWidth;
		}
		if (client->sizeHints.minHeight > 0 &&
		    height < client->sizeHints.minHeight) {
			height = client->sizeHints.minHeight;
		}

		if (client->sizeHints.maxWidth > 0 &&
		    width > client->sizeHints.maxWidth) {
			width = client->sizeHints.maxWidth;
		}
		if (client->sizeHints.maxHeight > 0 &&
		    height > client->sizeHints.maxHeight) {
			height = client->sizeHints.maxHeight;
		}
	}

	client->width  = width;
	client->height = height;

	XResizeWindow(display, client->window, client->width, client->height);

	if (windowResize.active && windowResize.client == client) {
		XRaiseWindow(display, client->window);
		if (!client->neverfocus) {
			XSetInputFocus(display, client->window,
				       RevertToPointerRoot, CurrentTime);
		}
	}

	configureClient(client);
}

void handleEnterNotify(XEvent *event)
{
	XCrossingEvent *ev = &event->xcrossing;

	fprintf(stderr, "Enter notify event for window 0x%lx (ignored)\n",
		ev->window);
}

void handleMapRequest(XEvent *event)
{
	XMapRequestEvent *ev = &event->xmaprequest;

	gettimeofday(&lastWindowOperation, NULL);
	lastMappedWindow = ev->window;

	manageClient(ev->window);

	SClient *client = findClient(ev->window);
	if (client) {
		SMonitor *monitor = &monitors[client->monitor];
		Atom	  state	  = getAtomProperty(client, NET_WM_STATE);
		if (state == NET_WM_STATE_FULLSCREEN) {
			fprintf(stderr, "Detected fullscreen window during map "
					"request, forcing proper position\n");

			client->oldx	  = client->x;
			client->oldy	  = client->y;
			client->oldwidth  = client->width;
			client->oldheight = client->height;

			client->x	     = monitor->x;
			client->y	     = monitor->y;
			client->width	     = monitor->width;
			client->height	     = monitor->height;
			client->isFullscreen = 1;
			client->isFloating   = 1;

			XMoveResizeWindow(display, client->window, client->x,
					  client->y, client->width,
					  client->height);
			XSetWindowBorderWidth(display, client->window, 0);
			configureClient(client);
			XRaiseWindow(display, client->window);
		}

		int	 hasFullscreenWindow = 0;
		SClient *fullscreenClient    = NULL;
		if (!client->isFullscreen) {
			for (SClient *c = clients; c; c = c->next) {
				if (c->monitor == client->monitor &&
				    c->workspace == client->workspace &&
				    c->isFullscreen) {
					hasFullscreenWindow = 1;
					fullscreenClient    = c;
					break;
				}
			}
		}

		if (client->isDock || client->workspace == DOCK_WORKSPACE) {
			XMapWindow(display, ev->window);
			XRaiseWindow(display, ev->window);
			fprintf(stderr, "Mapping dock window during map "
					"request\n");
		} else if (client->workspace == monitor->currentWorkspace &&
			   !hasFullscreenWindow) {
			XMapWindow(display, ev->window);
			focusClient(client);
		} else {
			XUnmapWindow(display, ev->window);

			if (hasFullscreenWindow && fullscreenClient) {
				focusClient(fullscreenClient);
			}
		}

		if (!client->isFloating && !client->isFullscreen) {
			XSync(display, False);
			arrangeClients(monitor);
		}

		restackFloatingWindows();
	} else {
		XMapWindow(display, ev->window);
	}
}

void handleConfigureRequest(XEvent *event)
{
	XConfigureRequestEvent *ev = &event->xconfigurerequest;
	XWindowChanges		wc;
	wc.x		= ev->x;
	wc.y		= ev->y;
	wc.width	= ev->width;
	wc.height	= ev->height;
	wc.border_width = borderWidth;
	wc.sibling	= ev->above;
	wc.stack_mode	= ev->detail;

	SClient *client = findClient(ev->window);
	if (client) {
		if (client->isFullscreen) {
			fprintf(stderr, "Intercepting configure request for "
					"fullscreen window\n");
			SMonitor *monitor = &monitors[client->monitor];
			wc.x		  = monitor->x;
			wc.y		  = monitor->y;
			wc.width	  = monitor->width;
			wc.height	  = monitor->height;
			wc.border_width	  = 0;
		} else if (!client->isFloating) {
			fprintf(stderr, "Intercepting configure request for "
					"tiled window\n");
			wc.x		= client->x;
			wc.y		= client->y;
			wc.width	= client->width;
			wc.height	= client->height;
			wc.border_width = borderWidth;
		}
	}

	XConfigureWindow(display, ev->window, ev->value_mask, &wc);

	if (client) {
		configureClient(client);
		restackFloatingWindows();
	}
}

void handleUnmapNotify(XEvent *event)
{
	XUnmapEvent *ev = &event->xunmap;

	gettimeofday(&lastWindowOperation, NULL);

	if (ev->send_event) {
		SClient *client = findClient(ev->window);
		if (client) {
			unmanageClient(ev->window);
		}
	}
}

void handleDestroyNotify(XEvent *event)
{
	XDestroyWindowEvent *ev = &event->xdestroywindow;

	gettimeofday(&lastWindowOperation, NULL);

	SClient *client = findClient(ev->window);

	if (client) {
		unmanageClient(ev->window);
	}
}

void spawnProgram(const char *program)
{
	if (!program) {
		return;
	}

	pid_t pid = fork();

	if (pid == -1) {
		fprintf(stderr, "banana: fork failed for program '%s'\n",
			program);
		return;
	}

	if (pid == 0) {
		if (display) {
			close(ConnectionNumber(display));
		}

		setsid();

		int devnull = open("/dev/null", O_RDWR);
		if (devnull != -1) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > 2) {
				close(devnull);
			}
		}

		execl("/bin/sh", "sh", "-c", program, NULL);
		fprintf(stderr, "banana: execl failed for program '%s': %s\n",
			program, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void killClient(const char *arg)
{
	(void)arg;

	if (!focused) {
		return;
	}

	if (focused->swallowed) {
		fprintf(stderr,
			"Not killing client 0x%lx - it's swallowing another "
			"window\n",
			focused->window);
		return;
	}

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

void quit(const char *arg)
{
	int exitCode = EXIT_SUCCESS;

	if (arg && *arg) {
		char *endptr;
		long  parsedCode = strtol(arg, &endptr, 10);

		if (*endptr == '\0' && parsedCode >= 0 && parsedCode <= 255) {
			exitCode = (int)parsedCode;
		}
	}

	cleanup();
	exit(exitCode);
}

void grabKeys()
{
	XUngrabKey(display, AnyKey, AnyModifier, root);

	for (size_t i = 0; i < keysCount; i++) {
		XGrabKey(display, XKeysymToKeycode(display, keys[i].keysym),
			 keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);

		XGrabKey(display, XKeysymToKeycode(display, keys[i].keysym),
			 keys[i].mod | LockMask, root, True, GrabModeAsync,
			 GrabModeAsync);
	}

	fprintf(stderr, "Key grabs set up on root window\n");
}

void updateFocus()
{
	if (!focused) {
		XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(display, root, NET_ACTIVE_WINDOW);
		return;
	}

	XSetInputFocus(display, focused->window, RevertToPointerRoot,
		       CurrentTime);
	XChangeProperty(display, root, NET_ACTIVE_WINDOW, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)&focused->window, 1);
	updateBorders();
}

void focusClient(SClient *client)
{
	if (!client) {
		return;
	}

	fprintf(stderr, "Attempting to focus: 0x%lx\n", client->window);

	XWindowAttributes wa;
	if (!XGetWindowAttributes(display, client->window, &wa)) {
		fprintf(stderr, "  Window no longer exists\n");
		return;
	}

	if (wa.map_state != IsViewable) {
		fprintf(stderr, "  Window not viewable (state: %d)\n",
			wa.map_state);
		return;
	}

	if (wa.override_redirect) {
		fprintf(stderr, "  Window has override_redirect set\n");
		return;
	}

	if (focused && focused != client) {
		lastFocused = focused;
	}

	focused = client;

	if (no_warps) {
		forcedMonitor = client->monitor;
	}

	if (!client->isFloating && !client->isFullscreen) {
		SMonitor *monitor = &monitors[client->monitor];
		monitor->lastTiledClient[client->workspace] = client->window;
		fprintf(stderr,
			"Updating last tiled client for workspace %d to "
			"0x%lx\n",
			client->workspace, client->window);
	}

	if ((windowMovement.active && windowMovement.client == client) ||
	    (windowResize.active && windowResize.client == client)) {
		XRaiseWindow(display, client->window);
	}

	static unsigned long activeBorder = 0;
	if (activeBorder == 0) {
		XColor	 color;
		Colormap cmap =
		    DefaultColormap(display, DefaultScreen(display));

		if (XAllocNamedColor(display, cmap, activeBorderColor, &color,
				     &color)) {
			activeBorder = color.pixel | 0xFF000000;
		} else {
			activeBorder =
			    BlackPixel(display, DefaultScreen(display)) |
			    0xFF000000;
		}
	}

	SMonitor *monitor = &monitors[client->monitor];
	if (!client->isFullscreen && !client->isDock &&
	    !(monitor->currentLayout == LAYOUT_MONOCLE &&
	      !client->isFloating)) {
		XSetWindowBorder(display, client->window, activeBorder);
	}

	if (!client->neverfocus) {
		XSetInputFocus(display, client->window, RevertToPointerRoot,
			       CurrentTime);
		XChangeProperty(display, root, NET_ACTIVE_WINDOW, XA_WINDOW, 32,
				PropModeReplace,
				(unsigned char *)&client->window, 1);
	}

	sendEvent(client, WM_TAKE_FOCUS);
	client->isUrgent = 0;
	updateClientUrgency(client);

	updateBorders();
	restackFloatingWindows();
	updateBars();
}

void manageClient(Window window)
{
	if (findClient(window)) {
		return;
	}

	XWindowAttributes wa;
	if (!XGetWindowAttributes(display, window, &wa)) {
		fprintf(stderr,
			"Cannot manage window 0x%lx: failed to get "
			"attributes\n",
			window);
		return;
	}

	if (wa.override_redirect) {
		fprintf(stderr, "Skipping override_redirect window 0x%lx\n",
			window);
		return;
	}

	SClient *client = malloc(sizeof(SClient));
	if (!client) {
		fprintf(stderr, "Failed to allocate memory for client\n");
		return;
	}

	int	     monitorNum = 0;
	int	     cursorX	= 0;
	int	     cursorY	= 0;

	int	     rootX, rootY;
	unsigned int mask;
	Window	     root_return, child_return;

	if (no_warps && forcedMonitor >= 0 && forcedMonitor < numMonitors) {
		monitorNum = forcedMonitor;
	} else if (XQueryPointer(display, root, &root_return, &child_return,
				 &rootX, &rootY, &cursorX, &cursorY, &mask)) {
		monitorNum = monitorAtPoint(rootX, rootY)->num;
		fprintf(stderr,
			"Using monitor %d at cursor position for new window\n",
			monitorNum);
	} else if (focused) {
		monitorNum = focused->monitor;
		fprintf(stderr,
			"Falling back to focused monitor %d for new window\n",
			monitorNum);
	}

	client->monitor		= monitorNum;
	client->workspace	= monitors[monitorNum].currentWorkspace;
	client->oldWorkspace	= client->workspace;
	client->window		= window;
	client->isFloating	= 0;
	client->isFullscreen	= 0;
	client->isDock		= 0;
	client->isUrgent	= 0;
	client->neverfocus	= 0;
	client->oldState	= 0;
	client->oldx		= 0;
	client->oldy		= 0;
	client->oldwidth	= 0;
	client->oldheight	= 0;
	client->width		= 0;
	client->height		= 0;
	client->x		= 0;
	client->y		= 0;
	client->sizeHints.valid = 0;
	client->swallowed	= NULL;
	client->swallowedBy	= NULL;
	client->isSwallowing	= 0;
	client->noswallow	= 0;
	client->pid		= getWindowPID(window);

	Window transientFor = None;
	if (XGetTransientForHint(display, window, &transientFor)) {
		SClient *parent = findClient(transientFor);
		if (parent) {
			client->monitor	   = parent->monitor;
			client->workspace  = parent->workspace;
			client->isFloating = 1;

			client->x = parent->x + 50;
			client->y = parent->y + 50;

			fprintf(stderr,
				"Transient window detected, attached to parent "
				"0x%lx\n",
				transientFor);
		}
	}

	SMonitor *monitor = &monitors[client->monitor];

	updateSizeHints(client);

	int	 willBeSwallowed = 0;
	SClient *potentialParent = NULL;

	if (client->pid > 0) {
		for (SClient *c = clients; c; c = c->next) {
			if (c == client) {
				continue;
			}

			if (c->isSwallowing && !c->swallowed &&
			    c->monitor == client->monitor &&
			    c->workspace == client->workspace) {
				if (isChildProcess(c->pid, client->pid)) {
					willBeSwallowed = 1;
					potentialParent = c;
					fprintf(stderr,
						"Window 0x%lx will be "
						"swallowed by 0x%lx - "
						"preparing in advance\n",
						window, c->window);
					break;
				}
			}
		}
	}

	if (willBeSwallowed && potentialParent && potentialParent->isFloating) {
		client->isFloating = 1;
		client->x	   = potentialParent->x;
		client->y	   = potentialParent->y;
		client->width	   = potentialParent->width;
		client->height	   = potentialParent->height;
		fprintf(stderr,
			"Pre-setting window 0x%lx as floating with parent "
			"geometry: %dx%d at %d,%d\n",
			window, client->width, client->height, client->x,
			client->y);
	}

	applyRules(client);

	if (client->sizeHints.valid && client->sizeHints.maxWidth &&
	    client->sizeHints.maxHeight && client->sizeHints.minWidth &&
	    client->sizeHints.minHeight &&
	    client->sizeHints.maxWidth == client->sizeHints.minWidth &&
	    client->sizeHints.maxHeight == client->sizeHints.minHeight) {
		client->isFloating = 1;
		fprintf(stderr, "Auto-floating fixed size window: %dx%d\n",
			client->sizeHints.minWidth,
			client->sizeHints.minHeight);
	}

	monitor = &monitors[client->monitor];

	if (client->width == 0 || client->height == 0) {
		if (wa.width > monitor->width - 2 * borderWidth) {
			client->width = monitor->width - 2 * borderWidth;
		} else {
			client->width = wa.width;
		}

		if (wa.height > monitor->height - 2 * borderWidth) {
			client->height = monitor->height - 2 * borderWidth;
		} else {
			client->height = wa.height;
		}

		if (client->sizeHints.valid && client->isFloating) {
			if (client->sizeHints.minWidth > 0 &&
			    client->width < client->sizeHints.minWidth) {
				client->width = client->sizeHints.minWidth;
			}
			if (client->sizeHints.minHeight > 0 &&
			    client->height < client->sizeHints.minHeight) {
				client->height = client->sizeHints.minHeight;
			}
		}

		if (!willBeSwallowed || !potentialParent ||
		    !potentialParent->isFloating) {
			client->x =
			    monitor->x + (monitor->width - client->width) / 2;
			client->y =
			    monitor->y + (monitor->height - client->height) / 2;
		}
	}

	if (client->y < monitor->y + barHeight && !client->isFloating) {
		client->y = monitor->y + barHeight;
	}

	if (client->isFloating) {
		if (client->x == 0 && client->y == 0 &&
		    (!willBeSwallowed || !potentialParent)) {
			client->x =
			    monitor->x + (monitor->width - client->width) / 2;
			client->y =
			    monitor->y + (monitor->height - client->height) / 2;
		}

		if (client->x + client->width < monitor->x) {
			client->x = monitor->x;
		}
		if (client->y + client->height < monitor->y) {
			client->y = monitor->y;
		}
		if (client->x > monitor->x + monitor->width) {
			client->x = monitor->x + monitor->width - client->width;
		}
		if (client->y > monitor->y + monitor->height) {
			client->y =
			    monitor->y + monitor->height - client->height;
		}

		if (client->y < monitor->y + barHeight) {
			client->y = monitor->y + barHeight;
		}
	}

	client->next = NULL;
	if (!clients) {
		clients = client;
	} else {
		if (!client->isFloating && newAsMaster) {
			client->next = clients;
			clients	     = client;
		} else {
			SClient *last = clients;
			while (last->next) {
				last = last->next;
			}
			last->next = client;
		}
	}

	XMoveResizeWindow(display, window, client->x, client->y, client->width,
			  client->height);

	XSetWindowBorderWidth(display, window, borderWidth);

	XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

	XSelectInput(display, window,
		     EnterWindowMask | FocusChangeMask | PropertyChangeMask |
			 StructureNotifyMask | PointerMotionMask);

	updateWindowType(client);
	updateWMHints(client);

	Atom	       actual_type;
	int	       actual_format;
	unsigned long  nitems, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(display, window, NET_WM_DESKTOP, 0, 1, False,
			       XA_CARDINAL, &actual_type, &actual_format,
			       &nitems, &bytes_after, &data) == Success &&
	    data) {
		if (actual_type == XA_CARDINAL && actual_format == 32 &&
		    nitems == 1) {
			long desktop = *(long *)data;

			if (desktop >= 0 && desktop < workspaceCount) {
				fprintf(stderr,
					"Window 0x%lx has existing "
					"_NET_WM_DESKTOP = %ld\n",
					window, desktop);
				client->workspace = desktop;
			}
		}
		XFree(data);
	}

	if (!client->isDock && !client->isFullscreen) {
		XGrabButton(display, Button1, modkey, window, False,
			    ButtonPressMask | ButtonReleaseMask |
				ButtonMotionMask,
			    GrabModeAsync, GrabModeAsync, None, moveCursor);
	}

	if (client->isFloating && !client->isDock) {
		XGrabButton(display, Button3, modkey, window, False,
			    ButtonPressMask | ButtonReleaseMask |
				ButtonMotionMask,
			    GrabModeAsync, GrabModeAsync, None, resizeSECursor);
	}

	XSync(display, False);
	XSetErrorHandler(oldHandler);

	fprintf(stderr,
		"Client managed: 0x%lx on monitor %d at position %d,%d with "
		"size %dx%d\n",
		window, client->monitor, client->x, client->y, client->width,
		client->height);

	updateBorders();

	XChangeProperty(display, root, NET_CLIENT_LIST, XA_WINDOW, 32,
			PropModeAppend, (unsigned char *)&window, 1);
	setClientState(client, NormalState);

	configureClient(client);
	updateClientDesktop(client);
	updateClientAllowedActions(client);

	if (client->isDock) {
		XMapWindow(display, client->window);
		XRaiseWindow(display, client->window);
		fprintf(stderr, "Mapping dock window 0x%lx immediately\n",
			client->window);

		if (barVisible) {
			fprintf(stderr,
				"Hiding bar because a dock was "
				"detected on monitor %d\n",
				client->monitor);
			barVisible = 0;
			showHideBars(0);
			updateClientPositionsForBar();
		}
	} else if (wa.map_state == IsViewable) {
		fprintf(stderr, "Window is viewable, focusing now\n");
		focusClient(client);
	} else {
		fprintf(stderr,
			"Window not yet viewable (state: %d), deferring "
			"focus\n",
			wa.map_state);
	}

	arrangeClients(monitor);

	if (!client->isFloating && !client->isFullscreen) {
		XLowerWindow(display, client->window);
	}

	restackFloatingWindows();

	updateBars();

	updateClientList();
	updateClientListStacking();
	updateClientDesktop(client);
	updateClientAllowedActions(client);

	if (client->pid > 0) {
		trySwallowClient(client);
	}
}

void unmanageClient(Window window)
{
	SClient *client = clients;
	SClient *prev	= NULL;

	while (client && client->window != window) {
		prev   = client;
		client = client->next;
	}

	if (!client) {
		return;
	}

	if (lastFocused == client) {
		lastFocused = NULL;
	}

	int wasClientDock = client->isDock;

	if (!client->isFloating && !client->isFullscreen) {
		SMonitor *mon = &monitors[client->monitor];
		if (mon->lastTiledClient[client->workspace] == client->window) {
			fprintf(stderr,
				"Last tiled client for workspace %d is being "
				"removed\n",
				client->workspace);

			Window newLastTiled = None;

			for (SClient *c = clients; c; c = c->next) {
				if (c != client && !c->isFloating &&
				    !c->isFullscreen &&
				    c->monitor == client->monitor &&
				    c->workspace == client->workspace) {
					newLastTiled = c->window;
					break;
				}
			}

			mon->lastTiledClient[client->workspace] = newLastTiled;
			fprintf(stderr,
				"New last tiled client for workspace %d: "
				"0x%lx\n",
				client->workspace, newLastTiled);
		}
	}

	if (client == focused) {
		SMonitor *currentMonitor   = &monitors[client->monitor];
		int	  currentWorkspace = client->workspace;

		SClient	 *clientToFocus	 = NULL;
		SClient	 *tiledClient	 = NULL;
		SClient	 *floatingClient = NULL;

		for (SClient *c = clients; c; c = c->next) {
			if (c != client && c->monitor == client->monitor &&
			    c->workspace == currentWorkspace) {
				if (!c->isFloating && !c->isFullscreen) {
					if (newAsMaster) {
						tiledClient = c;
						break;
					} else {
						tiledClient = c;
					}
				} else if (!floatingClient) {
					floatingClient = c;
				}
			}
		}

		clientToFocus = tiledClient ? tiledClient : floatingClient;

		if (clientToFocus) {
			fprintf(stderr,
				"Window closed, focusing %s client "
				"in workspace (tiled: %d)\n",
				newAsMaster ? "master" : "last",
				clientToFocus && !clientToFocus->isFloating);
			focused = clientToFocus;

			if (no_warps) {
				forcedMonitor = client->monitor;
			}

			if (currentMonitor->currentLayout == LAYOUT_MONOCLE &&
			    !clientToFocus->isFloating &&
			    !clientToFocus->isFullscreen) {
				currentMonitor
				    ->lastTiledClient[currentWorkspace] =
				    clientToFocus->window;
				fprintf(stderr,
					"Setting new last tiled client for "
					"monocle: 0x%lx\n",
					clientToFocus->window);
			}
		} else {
			fprintf(stderr,
				"Window closed, no other windows in workspace, "
				"focusing monitor %d\n",
				currentMonitor->num);
			focused = NULL;
			XDeleteProperty(display, root, NET_ACTIVE_WINDOW);
		}

		updateFocus();
	}

	SMonitor *monitor = &monitors[client->monitor];

	SClient	 *swallowedBy = client->swallowedBy;
	SClient	 *swallowed   = client->swallowed;

	if (prev) {
		prev->next = client->next;
	} else {
		clients = client->next;
	}

	if (swallowedBy) {
		fprintf(stderr, "Cleaning up swallow relationship - child "
				"window closed\n");
		swallowedBy->swallowed = swallowed;

		if (swallowed) {
			fprintf(stderr, "Transferring nested swallow "
					"relationship to parent\n");
			swallowed->swallowedBy = swallowedBy;
		} else {
			remapSwallowedClient(client);
		}
	} else if (swallowed) {
		fprintf(stderr, "Cleaning up swallow relationship - parent "
				"window closed\n");
		swallowed->swallowedBy = NULL;

		swallowed->workspace = swallowed->oldWorkspace;

		if (client->workspace ==
		    monitors[client->monitor].currentWorkspace) {
			fprintf(stderr, "Focusing previously swallowed "
					"window\n");
			focusClient(swallowed);
		}
	}

	int x	   = client->x;
	int y	   = client->y;
	int width  = client->width;
	int height = client->height;

	free(client);

	arrangeClients(monitor);

	if (wasClientDock) {
		if (!hasDocksOnMonitor(monitor->num) && !barVisible &&
		    showBar) {
			fprintf(stderr, "Last dock closed, showing bar\n");
			barVisible = 1;
			showHideBars(1);
			updateClientPositionsForBar();
		}

		for (int i = 0; i < numMonitors; i++) {
			SMonitor *m = &monitors[i];
			if (i != monitor->num && x < m->x + m->width &&
			    x + width > m->x && y < m->y + m->height &&
			    y + height > m->y) {
				fprintf(stderr,
					"Arranging monitor %d after dock "
					"removal\n",
					i);
				arrangeClients(&monitors[i]);
			}
		}
	}

	updateClientVisibility();
	updateBars();

	updateClientList();
	updateClientListStacking();
}

void configureClient(SClient *client)
{
	if (!client) {
		return;
	}

	SMonitor *monitor = &monitors[client->monitor];
	int	  noBorder =
	    client->isFullscreen || client->isDock ||
	    (monitor->currentLayout == LAYOUT_MONOCLE && !client->isFloating);

	XWindowChanges wc;
	wc.x		= client->x;
	wc.y		= client->y;
	wc.width	= client->width;
	wc.height	= client->height;
	wc.border_width = noBorder ? 0 : borderWidth;
	wc.sibling	= None;
	wc.stack_mode	= Above;

	XConfigureWindow(display, client->window,
			 CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);

	XEvent event;
	event.type			   = ConfigureNotify;
	event.xconfigure.display	   = display;
	event.xconfigure.event		   = client->window;
	event.xconfigure.window		   = client->window;
	event.xconfigure.x		   = client->x;
	event.xconfigure.y		   = client->y;
	event.xconfigure.width		   = client->width;
	event.xconfigure.height		   = client->height;
	event.xconfigure.border_width	   = noBorder ? 0 : borderWidth;
	event.xconfigure.above		   = None;
	event.xconfigure.override_redirect = False;
	XSendEvent(display, client->window, False, StructureNotifyMask, &event);

	XSetWindowBorderWidth(display, client->window,
			      noBorder ? 0 : borderWidth);

	updateBorders();
	XSync(display, False);
}

void updateBorders()
{
	static unsigned long activeBorder	     = 0;
	static unsigned long inactiveBorder	     = 0;
	static char	    *lastActiveBorderColor   = NULL;
	static char	    *lastInactiveBorderColor = NULL;

	if ((lastActiveBorderColor == NULL && activeBorderColor != NULL) ||
	    (lastActiveBorderColor != NULL && activeBorderColor != NULL &&
	     strcmp(lastActiveBorderColor, activeBorderColor) != 0) ||
	    (lastInactiveBorderColor == NULL && inactiveBorderColor != NULL) ||
	    (lastInactiveBorderColor != NULL && inactiveBorderColor != NULL &&
	     strcmp(lastInactiveBorderColor, inactiveBorderColor) != 0)) {
		if (lastActiveBorderColor) {
			free(lastActiveBorderColor);
			lastActiveBorderColor = NULL;
		}

		if (lastInactiveBorderColor) {
			free(lastInactiveBorderColor);
			lastInactiveBorderColor = NULL;
		}

		activeBorder   = 0;
		inactiveBorder = 0;
	}

	if (activeBorder == 0 || inactiveBorder == 0) {
		XColor	 color;
		Colormap cmap =
		    DefaultColormap(display, DefaultScreen(display));

		if (XAllocNamedColor(display, cmap, activeBorderColor, &color,
				     &color)) {
			activeBorder = color.pixel | 0xFF000000;
		} else {
			activeBorder =
			    BlackPixel(display, DefaultScreen(display)) |
			    0xFF000000;
		}

		if (XAllocNamedColor(display, cmap, inactiveBorderColor, &color,
				     &color)) {
			inactiveBorder = color.pixel | 0xFF000000;
		} else {
			inactiveBorder =
			    BlackPixel(display, DefaultScreen(display)) |
			    0xFF000000;
		}

		lastActiveBorderColor	= safeStrdup(activeBorderColor);
		lastInactiveBorderColor = safeStrdup(inactiveBorderColor);

		fprintf(stderr, "Border colors initialized\n");
	}

	SClient *client = clients;
	while (client) {
		SMonitor *monitor = &monitors[client->monitor];
		if (!client->isFullscreen && !client->isDock &&
		    !(monitor->currentLayout == LAYOUT_MONOCLE &&
		      !client->isFloating)) {
			XSetWindowBorder(display, client->window,
					 inactiveBorder);
		}
		client = client->next;
	}

	if (focused) {
		SMonitor *monitor = &monitors[focused->monitor];
		if (!focused->isFullscreen && !focused->isDock &&
		    !(monitor->currentLayout == LAYOUT_MONOCLE &&
		      !focused->isFloating)) {
			XSetWindowBorder(display, focused->window,
					 activeBorder);
		}
	}

	lastFocused = NULL;
}

SClient *findClient(Window window)
{
	SClient *client = clients;
	while (client) {
		if (client->window == window) {
			return client;
		}
		client = client->next;
	}
	return NULL;
}

SMonitor *monitorAtPoint(int x, int y)
{
	if (numMonitors <= 1) {
		return &monitors[0];
	}

	for (int i = 0; i < numMonitors; i++) {
		if (x >= monitors[i].x &&
		    x < monitors[i].x + monitors[i].width &&
		    y >= monitors[i].y &&
		    y < monitors[i].y + monitors[i].height) {
			return &monitors[i];
		}
	}

	return &monitors[0];
}

void updateMonitors()
{
	if (monitors) {
		for (int i = 0; i < numMonitors; i++) {
			free(monitors[i].masterFactors);
			free(monitors[i].workspaceLayouts);
			free(monitors[i].lastTiledClient);
		}
		free(monitors);
	}

	XRRScreenResources *screenRes = NULL;
	monitors		      = NULL;
	int oldNumMonitors	      = numMonitors;
	numMonitors		      = 0;

	screenRes = XRRGetScreenResources(display, root);

	if (!screenRes || screenRes->noutput == 0) {
		numMonitors   = 1;
		monitors      = malloc(sizeof(SMonitor));
		monitors[0].x = 0;
		monitors[0].y = 0;
		monitors[0].width =
		    DisplayWidth(display, DefaultScreen(display));
		monitors[0].height =
		    DisplayHeight(display, DefaultScreen(display));
		monitors[0].num		     = 0;
		monitors[0].currentWorkspace = 0;
		monitors[0].currentLayout    = LAYOUT_TILED;
		monitors[0].masterCount	     = 1;
		monitors[0].masterFactors =
		    malloc(workspaceCount * sizeof(float));
		monitors[0].workspaceLayouts =
		    malloc(workspaceCount * sizeof(ELayout));
		monitors[0].lastTiledClient =
		    malloc(workspaceCount * sizeof(Window));
		for (int ws = 0; ws < workspaceCount; ws++) {
			monitors[0].masterFactors[ws] = defaultMasterFactor;
			monitors[0].workspaceLayouts[ws] =
			    strcasecmp(defaultLayout, "monocle") == 0
				? LAYOUT_MONOCLE
				: LAYOUT_TILED;
			monitors[0].lastTiledClient[ws] = None;
		}

		if (screenRes) {
			XRRFreeScreenResources(screenRes);
		}
	} else {
		for (int i = 0; i < screenRes->noutput; i++) {
			XRROutputInfo *outputInfo = XRRGetOutputInfo(
			    display, screenRes, screenRes->outputs[i]);
			if (outputInfo &&
			    outputInfo->connection == RR_Connected &&
			    outputInfo->crtc) {
				numMonitors++;
			}
			if (outputInfo) {
				XRRFreeOutputInfo(outputInfo);
			}
		}

		if (numMonitors == 0) {
			numMonitors   = 1;
			monitors      = malloc(sizeof(SMonitor));
			monitors[0].x = 0;
			monitors[0].y = 0;
			monitors[0].width =
			    DisplayWidth(display, DefaultScreen(display));
			monitors[0].height =
			    DisplayHeight(display, DefaultScreen(display));
			monitors[0].num		     = 0;
			monitors[0].currentWorkspace = 0;
			monitors[0].currentLayout    = LAYOUT_TILED;
			monitors[0].masterCount	     = 1;
			monitors[0].masterFactors =
			    malloc(workspaceCount * sizeof(float));
			monitors[0].workspaceLayouts =
			    malloc(workspaceCount * sizeof(ELayout));
			monitors[0].lastTiledClient =
			    malloc(workspaceCount * sizeof(Window));
			for (int ws = 0; ws < workspaceCount; ws++) {
				monitors[0].masterFactors[ws] =
				    defaultMasterFactor;
				monitors[0].workspaceLayouts[ws] =
				    strcasecmp(defaultLayout, "monocle") == 0
					? LAYOUT_MONOCLE
					: LAYOUT_TILED;
				monitors[0].lastTiledClient[ws] = None;
			}
		} else {
			monitors = malloc(numMonitors * sizeof(SMonitor));
			int monitorIndex = 0;

			for (int i = 0; i < screenRes->noutput &&
					monitorIndex < numMonitors;
			     i++) {
				XRROutputInfo *outputInfo = XRRGetOutputInfo(
				    display, screenRes, screenRes->outputs[i]);

				if (outputInfo &&
				    outputInfo->connection == RR_Connected &&
				    outputInfo->crtc) {
					XRRCrtcInfo *crtcInfo =
					    XRRGetCrtcInfo(display, screenRes,
							   outputInfo->crtc);

					if (crtcInfo) {
						monitors[monitorIndex].x =
						    crtcInfo->x;
						monitors[monitorIndex].y =
						    crtcInfo->y;
						monitors[monitorIndex].width =
						    crtcInfo->width;
						monitors[monitorIndex].height =
						    crtcInfo->height;
						monitors[monitorIndex].num =
						    monitorIndex;
						monitors[monitorIndex]
						    .currentWorkspace = 0;
						monitors[monitorIndex]
						    .currentLayout =
						    LAYOUT_TILED;
						monitors[monitorIndex]
						    .masterCount = 1;
						monitors[monitorIndex]
						    .masterFactors =
						    malloc(workspaceCount *
							   sizeof(float));
						monitors[monitorIndex]
						    .workspaceLayouts =
						    malloc(workspaceCount *
							   sizeof(ELayout));
						monitors[monitorIndex]
						    .lastTiledClient =
						    malloc(workspaceCount *
							   sizeof(Window));

						for (int ws = 0;
						     ws < workspaceCount;
						     ws++) {
							monitors[monitorIndex]
							    .masterFactors[ws] =
							    defaultMasterFactor;
							monitors[monitorIndex]
							    .workspaceLayouts[ws] =
							    strcasecmp(
								defaultLayout,
								"monocle") == 0
								? LAYOUT_MONOCLE
								: LAYOUT_TILED;
							monitors[monitorIndex]
							    .lastTiledClient[ws] =
							    None;
						}

						monitorIndex++;
						XRRFreeCrtcInfo(crtcInfo);
					}
				}

				if (outputInfo) {
					XRRFreeOutputInfo(outputInfo);
				}
			}

			if (monitorIndex < numMonitors) {
				numMonitors = monitorIndex > 0 ? monitorIndex
							       : 1;
				if (monitorIndex == 0) {
					monitors[0].x	  = 0;
					monitors[0].y	  = 0;
					monitors[0].width = DisplayWidth(
					    display, DefaultScreen(display));
					monitors[0].height = DisplayHeight(
					    display, DefaultScreen(display));
					monitors[0].num		     = 0;
					monitors[0].currentWorkspace = 0;
					monitors[0].currentLayout =
					    LAYOUT_TILED;
					monitors[0].masterCount	  = 1;
					monitors[0].masterFactors = malloc(
					    workspaceCount * sizeof(float));
					monitors[0].workspaceLayouts = malloc(
					    workspaceCount * sizeof(ELayout));
					monitors[0].lastTiledClient = malloc(
					    workspaceCount * sizeof(Window));
					for (int ws = 0; ws < workspaceCount;
					     ws++) {
						monitors[0].masterFactors[ws] =
						    defaultMasterFactor;
						monitors[0]
						    .workspaceLayouts[ws] =
						    strcasecmp(defaultLayout,
							       "monocle") == 0
							? LAYOUT_MONOCLE
							: LAYOUT_TILED;
						monitors[0].lastTiledClient[ws] =
						    None;
					}
				}
			}
		}

		XRRFreeScreenResources(screenRes);
	}

	if (numMonitors != oldNumMonitors) {
		SClient *client = clients;
		while (client) {
			SMonitor *mon = getCurrentMonitor();
			int	  x   = client->x;
			int	  y   = client->y;
			if (x < mon->x || x >= mon->x + mon->width ||
			    y < mon->y || y >= mon->y + mon->height) {
				client->monitor = mon->num;
				client->x =
				    mon->x + (mon->width - client->width) / 2;
				client->y =
				    mon->y + (mon->height - client->height) / 2;
				if (client->y < mon->y + barHeight) {
					client->y = mon->y + barHeight;
				}
			}
			client = client->next;
		}

		updateDesktopViewport();
	}
}

void handlePropertyNotify(XEvent *event)
{
	XPropertyEvent *ev = &event->xproperty;

	if (ev->window == root && ev->atom == XA_WM_NAME) {
		updateStatus();
	} else if (ev->atom == XInternAtom(display, "_NET_WM_NAME", False) ||
		   ev->atom == XA_WM_NAME) {
		SClient *client = findClient(ev->window);
		if (client) {
			updateBars();
		}
	}

	SClient *client = findClient(ev->window);
	if (client) {
		if (ev->atom == XA_WM_NORMAL_HINTS || ev->atom == XA_WM_HINTS) {
			updateWMHints(client);
		} else if (ev->atom == NET_WM_WINDOW_TYPE) {
			updateWindowType(client);
		} else if (ev->atom == NET_WM_STATE) {
			Atom state = getAtomProperty(client, NET_WM_STATE);
			if (state == NET_WM_STATE_FULLSCREEN) {
				setFullscreen(client, 1);
			} else if (client->isFullscreen) {
				setFullscreen(client, 0);
			}
		}

		restackFloatingWindows();
	}
}

void handleExpose(XEvent *event)
{
	XExposeEvent *ev = &event->xexpose;

	for (int i = 0; i < numMonitors; i++) {
		if (barWindows && barWindows[i] == ev->window) {
			handleBarExpose(event);
			return;
		}
	}
}

SClient *focusWindowUnderCursor(SMonitor *monitor)
{
	int	     x, y;
	unsigned int mask;
	Window	     root_return, child_return;

	if (focused && (focused->monitor != monitor->num ||
			focused->workspace != monitor->currentWorkspace)) {
		focused = NULL;
		XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
		updateBorders();
	}

	if (lastCursorWarp) {
		return NULL;
	}

	if (XQueryPointer(display, root, &root_return, &child_return, &x, &y,
			  &x, &y, &mask)) {
		if (x >= monitor->x && x < monitor->x + monitor->width &&
		    y >= monitor->y && y < monitor->y + monitor->height) {
			SClient *windowUnderCursor = clientAtPoint(x, y);

			if (windowUnderCursor &&
			    windowUnderCursor->workspace ==
				monitor->currentWorkspace) {
				focusClient(windowUnderCursor);
				updateBars();
				return windowUnderCursor;
			}
		}
	}

	int activeMonitor = -1;
	if (focused) {
		activeMonitor = focused->monitor;
	}

	SClient *clientInWorkspace = findVisibleClientInWorkspace(
	    monitor->num, monitor->currentWorkspace);

	if (activeMonitor != monitor->num || !clientInWorkspace) {
		focused = NULL;
		XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
		updateBorders();
		updateBars();
	}
	return NULL;
}

void switchToWorkspace(const char *arg)
{
	if (!arg) {
		return;
	}

	int workspace = atoi(arg);
	if (workspace < 0 || workspace >= workspaceCount) {
		return;
	}

	SMonitor *monitor = getCurrentMonitor();
	if (monitor->currentWorkspace == workspace) {
		return;
	}

	fprintf(stderr, "Switching from workspace %d to %d on monitor %d\n",
		monitor->currentWorkspace, workspace, monitor->num);

	gettimeofday(&lastWindowOperation, NULL);

	focused = NULL;
	XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);

	monitor->currentWorkspace = workspace;

	if (no_warps) {
		forcedMonitor = monitor->num;
	}

	long currentDesktop = workspace;
	XChangeProperty(display, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&currentDesktop, 1);

	updateDesktopViewport();

	if (monitor->currentLayout == LAYOUT_MONOCLE &&
	    monitor->lastTiledClient[workspace] == None) {
		SClient *firstTiled = NULL;
		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == monitor->num &&
			    c->workspace == workspace && !c->isFloating &&
			    !c->isFullscreen) {
				firstTiled = c;
				break;
			}
		}

		if (firstTiled) {
			monitor->lastTiledClient[workspace] =
			    firstTiled->window;
			fprintf(stderr,
				"Setting lastTiledClient for workspace %d to "
				"0x%lx\n",
				workspace, firstTiled->window);
		}
	}

	updateClientVisibility();
	updateBars();

	arrangeClients(monitor);

	SClient *focusedClient = focusWindowUnderCursor(monitor);

	if (!focusedClient) {
		SClient *windowToFocus = NULL;

		if (monitor->currentLayout == LAYOUT_MONOCLE &&
		    monitor->lastTiledClient[workspace] != None) {
			SClient *lastTiled =
			    findClient(monitor->lastTiledClient[workspace]);

			if (lastTiled && lastTiled->monitor == monitor->num &&
			    lastTiled->workspace == workspace &&
			    !lastTiled->isFloating) {
				windowToFocus = lastTiled;
				fprintf(stderr,
					"Using last tiled client 0x%lx for "
					"workspace %d\n",
					lastTiled->window, workspace);
			}
		}

		if (!windowToFocus) {
			windowToFocus = findVisibleClientInWorkspace(
			    monitor->num, workspace);
		}

		if (windowToFocus) {
			fprintf(stderr,
				"Focusing window 0x%lx in workspace %d\n",
				windowToFocus->window, workspace);
			focusClient(windowToFocus);
		} else {
			currentWorkspace = workspace;
			if (focused && focused->monitor != monitor->num) {
				XSetInputFocus(display, root,
					       RevertToPointerRoot,
					       CurrentTime);
				focused = NULL;
				updateBorders();
			}

			XDeleteProperty(display, root, NET_ACTIVE_WINDOW);
			fprintf(stderr,
				"No windows in workspace %d, clearing "
				"NET_ACTIVE_WINDOW\n",
				workspace);
		}
	}
}

void moveClientToWorkspace(const char *arg)
{
	if (!arg || !focused) {
		return;
	}

	int workspace = atoi(arg);
	if (workspace < 0 || workspace >= workspaceCount) {
		return;
	}

	SMonitor *currentMon  = &monitors[focused->monitor];
	SClient	 *movedClient = focused;

	movedClient->oldWorkspace = movedClient->workspace;
	movedClient->workspace	  = workspace;

	moveClientToEnd(movedClient);
	updateClientDesktop(movedClient);

	if (workspace != currentMon->currentWorkspace) {
		XUnmapWindow(display, movedClient->window);

		SClient *focusedClient = focusWindowUnderCursor(currentMon);

		if (!focusedClient) {
			SClient *remainingWindow = findVisibleClientInWorkspace(
			    currentMon->num, currentMon->currentWorkspace);
			if (remainingWindow) {
				fprintf(stderr,
					"No window under cursor, focusing "
					"remaining window in workspace %d\n",
					currentMon->currentWorkspace);
				focusClient(remainingWindow);
			}
		}
	}

	arrangeClients(currentMon);

	for (int i = 0; i < numMonitors; i++) {
		if (i != currentMon->num &&
		    monitors[i].currentWorkspace == workspace) {
			arrangeClients(&monitors[i]);
			break;
		}
	}

	updateBars();
}

int hasDocks(void)
{
	for (SClient *c = clients; c; c = c->next) {
		if (c->isDock) {
			return 1;
		}
	}
	return 0;
}

int hasDocksOnMonitor(int monitorNum)
{
	SMonitor *m = &monitors[monitorNum];

	for (SClient *c = clients; c; c = c->next) {
		if (c->isDock) {
			if (c->x < m->x + m->width && c->x + c->width > m->x &&
			    c->y < m->y + m->height &&
			    c->y + c->height > m->y) {
				fprintf(stderr,
					"Found dock window 0x%lx overlapping "
					"monitor %d\n",
					c->window, monitorNum);
				return 1;
			}
		}
	}
	fprintf(stderr, "No docks found on monitor %d\n", monitorNum);
	return 0;
}

void updateClientVisibility()
{
	int	 hasFullscreen[MAX_MONITORS][100]     = {0};
	SClient *fullscreenClients[MAX_MONITORS][100] = {NULL};

	for (SClient *c = clients; c; c = c->next) {
		if (!c->isFullscreen || c->monitor >= MAX_MONITORS ||
		    c->workspace >= workspaceCount) {
			continue;
		}
		hasFullscreen[c->monitor][c->workspace]	    = 1;
		fullscreenClients[c->monitor][c->workspace] = c;
	}

	for (int i = 0; i < numMonitors; i++) {
		SMonitor *m = &monitors[i];
		if (m->currentLayout != LAYOUT_MONOCLE) {
			continue;
		}

		for (int ws = 0; ws < workspaceCount; ws++) {
			if (m->lastTiledClient[ws] == None) {
				continue;
			}

			SClient *lastClient =
			    findClient(m->lastTiledClient[ws]);
			if (lastClient && lastClient->monitor == i &&
			    lastClient->workspace == ws &&
			    !lastClient->isFloating &&
			    !lastClient->isFullscreen) {
				continue;
			}

			m->lastTiledClient[ws] = None;

			for (SClient *c = clients; c; c = c->next) {
				if (c->monitor != i || c->workspace != ws ||
				    c->isFloating || c->isFullscreen) {
					continue;
				}

				m->lastTiledClient[ws] = c->window;
				fprintf(stderr,
					"Resetting lastTiledClient to 0x%lx\n",
					c->window);
				break;
			}
		}
	}

	for (SClient *client = clients; client; client = client->next) {
		if (client->workspace == INT_MAX) {
			XUnmapWindow(display, client->window);
			continue;
		}

		if (client->isDock || client->workspace == DOCK_WORKSPACE) {
			XMapWindow(display, client->window);
			continue;
		}

		SMonitor *m = &monitors[client->monitor];
		if (client->workspace != m->currentWorkspace) {
			XUnmapWindow(display, client->window);
			continue;
		}

		if (windowMovement.active && windowMovement.client == client) {
			XMapWindow(display, client->window);
			XRaiseWindow(display, client->window);
			continue;
		}

		if (hasFullscreen[client->monitor][client->workspace]) {
			if (client->isFullscreen) {
				XMapWindow(display, client->window);
				XRaiseWindow(display, client->window);

				if (client != focused &&
				    client->workspace == m->currentWorkspace &&
				    client->monitor ==
					getCurrentMonitor()->num) {
					focusClient(client);
				}
			} else {
				XUnmapWindow(display, client->window);
			}
			continue;
		}

		// Normal monocle layout handling (no fullscreen windows)
		if (m->currentLayout == LAYOUT_MONOCLE && !client->isFloating &&
		    !client->isFullscreen) {
			if (client == focused ||
			    m->lastTiledClient[m->currentWorkspace] ==
				client->window) {
				XMapWindow(display, client->window);

				if (client != focused &&
				    m->lastTiledClient[m->currentWorkspace] ==
					client->window) {
					XRaiseWindow(display, client->window);
				}
			} else {
				XUnmapWindow(display, client->window);
			}
			continue;
		}

		XMapWindow(display, client->window);
	}
}

SClient *findVisibleClientInWorkspace(int monitor, int workspace)
{
	SClient	    *client = clients;
	int	     x, y;
	unsigned int mask;
	Window	     root_return, child_return;

	if (XQueryPointer(display, root, &root_return, &child_return, &x, &y,
			  &x, &y, &mask)) {
		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == monitor &&
			    c->workspace == workspace && x >= c->x &&
			    x < c->x + c->width && y >= c->y &&
			    y < c->y + c->height) {
				return c;
			}
		}
	}

	while (client) {
		if (client->monitor == monitor &&
		    client->workspace == workspace) {
			return client;
		}
		client = client->next;
	}

	return NULL;
}

SMonitor *getCurrentMonitor()
{
	if (no_warps && forcedMonitor >= 0 && forcedMonitor < numMonitors) {
		return &monitors[forcedMonitor];
	}

	int	     x, y;
	unsigned int mask;
	Window	     root_return, child_return;

	if (XQueryPointer(display, root, &root_return, &child_return, &x, &y,
			  &x, &y, &mask)) {
		return monitorAtPoint(x, y);
	}

	return &monitors[0];
}

void moveClientToEnd(SClient *client)
{
	if (!client || !clients) {
		return;
	}

	if (!client->next) {
		return;
	}

	if (clients == client) {
		clients = client->next;
	} else {
		SClient *prev = clients;
		while (prev && prev->next != client) {
			prev = prev->next;
		}

		if (!prev) {
			return;
		}

		prev->next = client->next;
	}

	SClient *last = clients;
	while (last->next) {
		last = last->next;
	}

	last->next   = client;
	client->next = NULL;
}

void toggleFloating(const char *arg)
{
	(void)arg;

	if (!focused) {
		return;
	}

	if (focused->isFullscreen) {
		return;
	}

	if (focused->isDock) {
		return;
	}

	int wasFloating = focused->isFloating;

	int isFixedSize =
	    (focused->sizeHints.valid && focused->sizeHints.maxWidth &&
	     focused->sizeHints.maxHeight && focused->sizeHints.minWidth &&
	     focused->sizeHints.minHeight &&
	     focused->sizeHints.maxWidth == focused->sizeHints.minWidth &&
	     focused->sizeHints.maxHeight == focused->sizeHints.minHeight);

	focused->isFloating = !focused->isFloating || isFixedSize;

	if (focused->isFloating) {
		SMonitor *monitor = &monitors[focused->monitor];

		if (monitor->currentLayout == LAYOUT_TILED ||
		    monitor->currentLayout == LAYOUT_MONOCLE) {
			int oldWidth  = focused->width;
			int oldHeight = focused->height;

			int newWidth  = oldWidth * 0.8;
			int newHeight = oldHeight * 0.8;

			newWidth  = MAX(20, newWidth);
			newHeight = MAX(10, newHeight);

			if (focused->sizeHints.valid) {
				if (focused->sizeHints.minWidth > 0 &&
				    newWidth < focused->sizeHints.minWidth) {
					newWidth = focused->sizeHints.minWidth;
				}
				if (focused->sizeHints.minHeight > 0 &&
				    newHeight < focused->sizeHints.minHeight) {
					newHeight =
					    focused->sizeHints.minHeight;
				}
			}

			int xOffset = (oldWidth - newWidth) / 2;
			int yOffset = (oldHeight - newHeight) / 2;
			int newX    = focused->x + xOffset;
			int newY    = focused->y + yOffset;

			focused->x	= newX;
			focused->y	= newY;
			focused->width	= newWidth;
			focused->height = newHeight;

			XMoveResizeWindow(display, focused->window, focused->x,
					  focused->y, focused->width,
					  focused->height);
		}

		XGrabButton(display, Button3, modkey, focused->window, False,
			    ButtonPressMask | ButtonReleaseMask |
				ButtonMotionMask,
			    GrabModeAsync, GrabModeAsync, None, resizeSECursor);

		XRaiseWindow(display, focused->window);
		if (!focused->neverfocus) {
			XSetInputFocus(display, focused->window,
				       RevertToPointerRoot, CurrentTime);
		}

		configureClient(focused);

		arrangeClients(&monitors[focused->monitor]);
	} else if (wasFloating) {
		XUngrabButton(display, Button3, modkey, focused->window);

		SMonitor *newMonitor =
		    monitorAtPoint(focused->x + focused->width / 2,
				   focused->y + focused->height / 2);

		if (newMonitor->num != focused->monitor) {
			int oldMonitor	   = focused->monitor;
			focused->monitor   = newMonitor->num;
			focused->workspace = newMonitor->currentWorkspace;

			arrangeClients(&monitors[oldMonitor]);
		}

		moveClientToEnd(focused);

		XLowerWindow(display, focused->window);

		if (!focused->isFloating && !focused->isFullscreen) {
			SMonitor *monitor = &monitors[focused->monitor];
			monitor->lastTiledClient[focused->workspace] =
			    focused->window;
			fprintf(stderr,
				"Updating last tiled client for workspace %d "
				"to 0x%lx "
				"(toggle floating)\n",
				focused->workspace, focused->window);
		}

		arrangeClients(&monitors[focused->monitor]);
	}

	updateBorders();
	restackFloatingWindows();
	updateBars();
}

void arrangeClients(SMonitor *monitor)
{
	if (!monitor) {
		return;
	}

	fprintf(stderr, "Arranging clients for monitor %d\n", monitor->num);

	if (strcasecmp(defaultLayout, "monocle") == 0) {
		monitor->currentLayout = LAYOUT_MONOCLE;
	} else {
		monitor->currentLayout = LAYOUT_TILED;
	}

	monitor->currentLayout =
	    monitor->workspaceLayouts[monitor->currentWorkspace];

	if (monitor->currentLayout == LAYOUT_MONOCLE) {
		monocleClients(monitor);
	} else {
		tileClients(monitor);
	}

	updateClientVisibility();
}

void monocleClients(SMonitor *monitor)
{
	if (!monitor) {
		return;
	}

	SClient	 *visibleClients[MAX_CLIENTS] = {NULL};
	int	  visibleCount		      = 0;
	SClient	 *focusedClient		      = NULL;
	SClient	 *lastTiledClient	      = NULL;
	SMonitor *currentMonitor	      = getCurrentMonitor();
	int	  isActiveMonitor = (monitor->num == currentMonitor->num);

	for (SClient *client = clients; client; client = client->next) {
		if (client->monitor == monitor->num &&
		    client->workspace == monitor->currentWorkspace &&
		    !client->isFloating && !client->isFullscreen &&
		    !client->isDock) {
			visibleClients[visibleCount++] = client;
			if (client == focused) {
				focusedClient = client;
			}
			if (monitor->lastTiledClient[monitor->currentWorkspace] ==
			    client->window) {
				lastTiledClient = client;
			}
			if (visibleCount >= MAX_CLIENTS) {
				break;
			}
		}
	}

	if (visibleCount == 0) {
		return;
	}

	if (!focusedClient && visibleCount > 0 && isActiveMonitor) {
		if (lastTiledClient) {
			focusedClient = lastTiledClient;
			fprintf(stderr,
				"Using last tiled client 0x%lx for workspace "
				"%d\n",
				focusedClient->window,
				monitor->currentWorkspace);
		} else {
			focusedClient = visibleClients[0];
			monitor->lastTiledClient[monitor->currentWorkspace] =
			    focusedClient->window;
			fprintf(stderr,
				"No last tiled client found, using first "
				"client 0x%lx\n",
				focusedClient->window);
		}

		monitor->lastTiledClient[monitor->currentWorkspace] =
		    focusedClient->window;

		focusClient(focusedClient);
	}

	int x		    = monitor->x;
	int y		    = monitor->y;
	int availableWidth  = monitor->width;
	int availableHeight = monitor->height;
	int dockHeight = getDockHeight(monitor->num, monitor->currentWorkspace);
	int docksPresent = hasDocksOnMonitor(monitor->num);
	int dockPosition = getDockPosition(monitor->num);

	if (barVisible && !docksPresent) {
		if (bottomBar) {
			availableHeight -=
			    (barHeight + barBorderWidth * 2 + barStrutsTop);
		} else {
			int barBottom = monitor->y + barStrutsTop + barHeight +
					barBorderWidth * 2;
			availableHeight -=
			    (barStrutsTop + barHeight + barBorderWidth * 2);
			y = barBottom;
		}
	}

	if (dockHeight > 0 && docksPresent) {
		availableHeight -= dockHeight;

		if (dockPosition == 1) {
			fprintf(stderr,
				"Adjusting monocle window position for bottom "
				"dock: %d on monitor %d\n",
				dockHeight, monitor->num);
		} else {
			y += dockHeight;
			fprintf(stderr,
				"Adjusting monocle window position for top "
				"dock: %d on monitor %d\n",
				dockHeight, monitor->num);
		}
	}

	int width  = availableWidth;
	int height = availableHeight;

	for (int i = 0; i < visibleCount; i++) {
		SClient *client = visibleClients[i];

		client->x      = x;
		client->y      = y;
		client->width  = width;
		client->height = height;

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		XSetWindowBorderWidth(display, client->window, 0);
		configureClient(client);

		if (client == focusedClient) {
			XRaiseWindow(display, client->window);
			XMapWindow(display, client->window);
		} else {
			XUnmapWindow(display, client->window);
		}
	}
}

void restackFloatingWindows()
{
	for (int m = 0; m < numMonitors; m++) {
		SMonitor *monitor = &monitors[m];

		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == monitor->num &&
			    c->workspace == monitor->currentWorkspace &&
			    c->isDock) {
				XLowerWindow(display, c->window);
			}
		}

		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == monitor->num &&
			    c->workspace == monitor->currentWorkspace &&
			    !c->isFloating && !c->isDock) {
				XLowerWindow(display, c->window);
			}
		}

		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == monitor->num &&
			    c->workspace == monitor->currentWorkspace &&
			    c->isFloating && !c->isDock) {
				if (c == focused && (windowMovement.active ||
						     windowResize.active)) {
					XRaiseWindow(display, c->window);
				}
			}
		}
	}
}

int makeWindowFloatIfNeeded(SClient *client, SMonitor *monitor, int width,
			    int height)
{
	if ((width <= 0 || height <= 0 || width < 10 || height < 10) &&
	    (lastMappedWindow == client->window)) {
		fprintf(stderr,
			"New window 0x%lx is too smol in tiled layout, "
			"making it float\n",
			client->window);

		client->isFloating = 1;

		client->width  = MIN(500, monitor->width * 0.6);
		client->height = MIN(400, monitor->height * 0.6);

		if (client->sizeHints.valid) {
			if (client->sizeHints.minWidth > 0 &&
			    client->width < client->sizeHints.minWidth) {
				client->width = client->sizeHints.minWidth;
			}
			if (client->sizeHints.minHeight > 0 &&
			    client->height < client->sizeHints.minHeight) {
				client->height = client->sizeHints.minHeight;
			}
		}

		client->x = monitor->x + (monitor->width - client->width) / 2;
		client->y = monitor->y + (monitor->height - client->height) / 2;

		if (client->y < monitor->y + barHeight) {
			client->y = monitor->y + barHeight;
		}

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);

		XGrabButton(display, Button3, modkey, client->window, False,
			    ButtonPressMask | ButtonReleaseMask |
				ButtonMotionMask,
			    GrabModeAsync, GrabModeAsync, None, resizeSECursor);

		return 1;
	}

	return 0;
}

void tileClients(SMonitor *monitor)
{
	if (!monitor) {
		return;
	}

	SClient *visibleClients[MAX_CLIENTS] = {NULL};
	int	 visibleCount		     = 0;
	int	 currentWorkspace	     = monitor->currentWorkspace;
	for (SClient *client = clients; client; client = client->next) {
		if (client->monitor == monitor->num &&
		    client->workspace == monitor->currentWorkspace &&
		    !client->isFloating && !client->isFullscreen &&
		    !client->isDock) {
			visibleClients[visibleCount++] = client;
			if (visibleCount >= MAX_CLIENTS) {
				break;
			}
		}
	}

	if (visibleCount <= 1) {
		monitor->masterFactors[currentWorkspace] = defaultMasterFactor;
	}

	float masterFactor = monitor->masterFactors[currentWorkspace];

	int   x		      = monitor->x + outerGap;
	int   y		      = monitor->y + outerGap;
	int   availableWidth  = monitor->width - (2 * outerGap);
	int   availableHeight = monitor->height - (2 * outerGap);
	int   dockHeight      = getDockHeight(monitor->num, currentWorkspace);
	int   docksPresent    = hasDocksOnMonitor(monitor->num);
	int   dockPosition    = getDockPosition(monitor->num);

	if (barVisible && !docksPresent) {
		if (bottomBar) {
			availableHeight -=
			    (barHeight + barBorderWidth * 2 + barStrutsTop);
		} else {
			int barBottom = monitor->y + barStrutsTop + barHeight +
					barBorderWidth * 2;
			availableHeight -=
			    (barStrutsTop + barHeight + barBorderWidth * 2);
			y = barBottom + outerGap;
		}
	}

	if (dockHeight > 0 && docksPresent) {
		availableHeight -= dockHeight;

		if (dockPosition == 1) {
			fprintf(stderr,
				"Adjusting tiled window positions for bottom "
				"dock: %d on monitor %d\n",
				dockHeight, monitor->num);
		} else {
			y += dockHeight;
			fprintf(stderr,
				"Adjusting tiled window positions for top "
				"dock: %d on monitor %d\n",
				dockHeight, monitor->num);
		}
	}

	int masterArea = availableWidth * masterFactor;
	int stackArea  = availableWidth - masterArea;

	if (visibleCount == 0) {
		return;
	}

	if (visibleCount == 1) {
		SClient *client = visibleClients[0];
		int	 width	= availableWidth - 2 * borderWidth;
		int	 height = availableHeight - 2 * borderWidth;

		client->x      = x;
		client->y      = y;
		client->width  = width;
		client->height = height;

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);
		fprintf(stderr,
			"Single window tiled: monitor=%d pos=%d,%d "
			"size=%dx%d\n",
			monitor->num, client->x, client->y, client->width,
			client->height);
		return;
	}

	int masterCount = MIN(monitor->masterCount, visibleCount);
	int stackCount	= visibleCount - masterCount;

	int masterHeight = 0;
	if (masterCount > 0) {
		masterHeight = availableHeight / masterCount;
	}

	int stackHeight = 0;
	if (stackCount > 0) {
		stackHeight = availableHeight / stackCount;
	}

	int masterRemainder = availableHeight % masterCount;
	int stackRemainder  = availableHeight % stackCount;

	int masterY = y;
	int stackY  = y;

	int masterWidth = masterArea - innerGap / 2 - 2 * borderWidth;
	int stackWidth	= stackArea - innerGap / 2 - 2 * borderWidth;
	int stackX	= x + masterArea + innerGap / 2;

	int madeFloat = 0;

	for (int i = 0; i < masterCount; i++) {
		SClient *client		  = visibleClients[i];
		int	 heightAdjustment = (i < masterRemainder) ? 1 : 0;
		int	 currentHeight	  = masterHeight + heightAdjustment;

		if (i > 0) {
			masterY += innerGap;
			currentHeight -= innerGap;
		}

		int width  = masterWidth;
		int height = currentHeight - 2 * borderWidth;

		if (makeWindowFloatIfNeeded(client, monitor, width, height)) {
			madeFloat = 1;
			continue;
		}

		client->x      = x;
		client->y      = masterY;
		client->width  = width;
		client->height = height;

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);

		masterY += currentHeight;
	}

	for (int i = 0; i < stackCount; i++) {
		SClient *client		  = visibleClients[i + masterCount];
		int	 heightAdjustment = (i < stackRemainder) ? 1 : 0;
		int	 currentHeight	  = stackHeight + heightAdjustment;

		if (i > 0) {
			stackY += innerGap;
			currentHeight -= innerGap;
		}

		int width  = stackWidth;
		int height = currentHeight - 2 * borderWidth;

		if (makeWindowFloatIfNeeded(client, monitor, width, height)) {
			madeFloat = 1;
			continue;
		}

		client->x      = stackX;
		client->y      = stackY;
		client->width  = width;
		client->height = height;

		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);

		stackY += currentHeight;
	}

	if (madeFloat) {
		arrangeClients(monitor);
	}
}

void warpPointerToClientCenter(SClient *client)
{
	if (!client) {
		return;
	}

	if (no_warps) {
		return;
	}

	int centerX = client->x + client->width / 2;
	int centerY = client->y + client->height / 2;

	XWarpPointer(display, None, root, 0, 0, 0, 0, centerX, centerY);
	lastCursorWarp = 1;
	gettimeofday(&lastWindowOperation, NULL);
}

void moveWindowInStack(const char *arg)
{
	if (!focused || !arg) {
		return;
	}

	if (focused->isFloating) {
		return;
	}

	SMonitor *monitor   = &monitors[focused->monitor];
	int	  workspace = focused->workspace;

	SClient	 *targetClient = NULL;

	SClient	 *workspaceClients[MAX_CLIENTS];
	int	  numClients   = 0;
	int	  focusedIndex = -1;

	for (SClient *c = clients; c; c = c->next) {
		if (c->monitor == focused->monitor &&
		    c->workspace == workspace && !c->isFloating &&
		    !c->isFullscreen) {
			if (c == focused) {
				focusedIndex = numClients;
			}

			workspaceClients[numClients++] = c;
			if (numClients >= MAX_CLIENTS) {
				break;
			}
		}
	}

	if (numClients <= 1) {
		return;
	}

	if (strcmp(arg, "up") == 0) {
		int targetIndex = (focusedIndex - 1 + numClients) % numClients;
		targetClient	= workspaceClients[targetIndex];
	} else if (strcmp(arg, "down") == 0) {
		int targetIndex = (focusedIndex + 1) % numClients;
		targetClient	= workspaceClients[targetIndex];
	}

	if (targetClient && targetClient != focused) {
		fprintf(stderr,
			"Moving window in stack: 0x%lx with 0x%lx (direction: "
			"%s)\n",
			focused->window, targetClient->window, arg);
		swapClients(focused, targetClient);
		arrangeClients(monitor);
		restackFloatingWindows();

		warpPointerToClientCenter(focused);
		gettimeofday(&lastWindowOperation, NULL);
		updateBorders();
	}
}

void focusWindowInStack(const char *arg)
{
	if (!focused || !arg) {
		return;
	}

	SMonitor *monitor = getCurrentMonitor();
	if (focused->monitor != monitor->num ||
	    focused->workspace != monitor->currentWorkspace) {
		return;
	}

	if (focused->isFullscreen && monitor->currentLayout == LAYOUT_MONOCLE) {
		return;
	}

	int	 workspace    = focused->workspace;
	SClient *targetClient = NULL;
	SClient *prevFocused  = focused;

	if (monitor->currentLayout == LAYOUT_MONOCLE) {
		SClient *monocleClients[MAX_CLIENTS] = {NULL};
		int	 numMonocle		     = 0;
		int	 focusedIndex		     = -1;

		for (SClient *c = clients; c; c = c->next) {
			if (c->monitor == focused->monitor &&
			    c->workspace == workspace && !c->isFloating &&
			    !c->isFullscreen) {
				if (c == focused) {
					focusedIndex = numMonocle;
				}
				monocleClients[numMonocle++] = c;
				if (numMonocle >= MAX_CLIENTS) {
					break;
				}
			}
		}

		if (numMonocle <= 1) {
			return;
		}

		if (strcmp(arg, "up") == 0) {
			focusedIndex =
			    (focusedIndex - 1 + numMonocle) % numMonocle;
		} else if (strcmp(arg, "down") == 0) {
			focusedIndex = (focusedIndex + 1) % numMonocle;
		}

		targetClient = monocleClients[focusedIndex];

		if (targetClient && targetClient != focused) {
			fprintf(stderr,
				"Cycling monocle window: 0x%lx (direction: "
				"%s)\n",
				targetClient->window, arg);

			XMapWindow(display, targetClient->window);
			XRaiseWindow(display, targetClient->window);

			XSync(display, False);

			focusClient(targetClient);

			if (!prevFocused->isFloating) {
				XUnmapWindow(display, prevFocused->window);
			}

			XSync(display, False);

			warpPointerToClientCenter(targetClient);
		}
		return;
	}

	SClient *tiledClients[MAX_CLIENTS];
	SClient *floatingClients[MAX_CLIENTS];
	int	 numTiled	      = 0;
	int	 numFloating	      = 0;
	int	 focusedTiledIndex    = -1;
	int	 focusedFloatingIndex = -1;

	for (SClient *c = clients; c; c = c->next) {
		if (c->monitor == focused->monitor &&
		    c->workspace == workspace && !c->isFullscreen) {
			if (c->isFloating) {
				if (c == focused) {
					focusedFloatingIndex = numFloating;
				}
				floatingClients[numFloating++] = c;
			} else {
				if (c == focused) {
					focusedTiledIndex = numTiled;
				}
				tiledClients[numTiled++] = c;
			}
			if (numTiled + numFloating >= MAX_CLIENTS) {
				break;
			}
		}
	}

	if (numTiled + numFloating <= 1) {
		return;
	}

	if (focused->isFloating) {
		if (strcmp(arg, "up") == 0) {
			if (focusedFloatingIndex > 0) {
				targetClient =
				    floatingClients[focusedFloatingIndex - 1];
			} else if (numTiled > 0) {
				targetClient = tiledClients[numTiled - 1];
			} else {
				targetClient = floatingClients[numFloating - 1];
			}
		} else if (strcmp(arg, "down") == 0) {
			if (focusedFloatingIndex < numFloating - 1) {
				targetClient =
				    floatingClients[focusedFloatingIndex + 1];
			} else if (numTiled > 0) {
				targetClient = tiledClients[0];
			} else {
				targetClient = floatingClients[0];
			}
		}
	} else {
		if (strcmp(arg, "up") == 0) {
			if (focusedTiledIndex > 0) {
				targetClient =
				    tiledClients[focusedTiledIndex - 1];
			} else if (numFloating > 0) {
				targetClient = floatingClients[numFloating - 1];
			} else {
				targetClient = tiledClients[numTiled - 1];
			}
		} else if (strcmp(arg, "down") == 0) {
			if (focusedTiledIndex < numTiled - 1) {
				targetClient =
				    tiledClients[focusedTiledIndex + 1];
			} else if (numFloating > 0) {
				targetClient = floatingClients[0];
			} else {
				targetClient = tiledClients[0];
			}
		}
	}

	if (targetClient && targetClient != focused) {
		fprintf(stderr,
			"Focusing window in stack: 0x%lx (direction: %s, "
			"floating: %d)\n",
			targetClient->window, arg, targetClient->isFloating);
		focusClient(targetClient);
		warpPointerToClientCenter(targetClient);
		gettimeofday(&lastWindowOperation, NULL);
	}
}

void adjustMasterFactor(const char *arg)
{
	if (!arg) {
		return;
	}

	SMonitor *monitor = getCurrentMonitor();

	if (monitor->currentLayout == LAYOUT_MONOCLE) {
		return;
	}

	int   workspace = monitor->currentWorkspace;
	float delta	= 0.05;

	if (strcmp(arg, "decrease") == 0) {
		delta = -delta;
	}

	monitor->masterFactors[workspace] += delta;

	if (monitor->masterFactors[workspace] < 0.1) {
		monitor->masterFactors[workspace] = 0.1;
	} else if (monitor->masterFactors[workspace] > 0.9) {
		monitor->masterFactors[workspace] = 0.9;
	}

	tileClients(monitor);
	for (SClient *c = clients; c; c = c->next) {
		if (c->monitor == monitor->num &&
		    c->workspace == monitor->currentWorkspace &&
		    !c->isFloating) {
			XLowerWindow(display, c->window);
		}
	}
}

void swapClients(SClient *a, SClient *b)
{
	if (!a || !b || a == b) {
		return;
	}

	fprintf(stderr, "Swapping clients in list: 0x%lx and 0x%lx\n",
		a->window, b->window);

	SClient *aNext = a->next;
	SClient *bNext = b->next;

	SClient *prevA = NULL;
	SClient *prevB = NULL;
	SClient *temp  = clients;

	while (temp && temp != a) {
		prevA = temp;
		temp  = temp->next;
	}

	temp = clients;
	while (temp && temp != b) {
		prevB = temp;
		temp  = temp->next;
	}

	if (a->next == b) {
		if (prevA) {
			prevA->next = b;
		} else {
			clients = b;
		}

		b->next = a;
		a->next = bNext;
	} else if (b->next == a) {
		if (prevB) {
			prevB->next = a;
		} else {
			clients = a;
		}

		a->next = b;
		b->next = aNext;
	} else {
		if (prevA) {
			prevA->next = b;
		} else {
			clients = b;
		}

		if (prevB) {
			prevB->next = a;
		} else {
			clients = a;
		}

		a->next = bNext;
		b->next = aNext;
	}

	if (a->next == a) {
		a->next = b;
	}
	if (b->next == b) {
		b->next = a;
	}

	restackFloatingWindows();
}

void updateClientList()
{
	Window	 windowList[MAX_CLIENTS];
	int	 count	= 0;
	SClient *client = clients;

	while (client && count < MAX_CLIENTS) {
		windowList[count++] = client->window;
		client		    = client->next;
	}

	XChangeProperty(display, root, NET_CLIENT_LIST, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)windowList, count);
}

void updateClientListStacking()
{
	Window windowList[MAX_CLIENTS];
	int    count = 0;

	for (SClient *client = clients; client && count < MAX_CLIENTS;
	     client	     = client->next) {
		if (client->isFloating || client->isFullscreen) {
			windowList[count++] = client->window;
		}
	}

	for (SClient *client = clients; client && count < MAX_CLIENTS;
	     client	     = client->next) {
		if (!client->isFloating && !client->isFullscreen) {
			windowList[count++] = client->window;
		}
	}

	XChangeProperty(display, root, NET_CLIENT_LIST_STACKING, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)windowList, count);

	fprintf(stderr, "Updated _NET_CLIENT_LIST_STACKING with %d windows\n",
		count);
}

Atom getAtomProperty(SClient *client, Atom prop)
{
	int	       di;
	unsigned long  dl;
	unsigned char *p = NULL;
	Atom	       da, atom = None;

	if (XGetWindowProperty(display, client->window, prop, 0L, sizeof atom,
			       False, XA_ATOM, &da, &di, &dl, &dl,
			       &p) == Success &&
	    p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

void setClientState(SClient *client, long state)
{
	long data[] = {state, None};

	XChangeProperty(display, client->window, WM_STATE, WM_STATE, 32,
			PropModeReplace, (unsigned char *)data, 2);
}

int sendEvent(SClient *client, Atom proto)
{
	int    n;
	Atom  *protocols;
	int    exists = 0;
	XEvent ev;

	if (XGetWMProtocols(display, client->window, &protocols, &n)) {
		while (!exists && n--) {
			exists = protocols[n] == proto;
		}
		XFree(protocols);
	}

	if (exists) {
		ev.type			= ClientMessage;
		ev.xclient.window	= client->window;
		ev.xclient.message_type = WM_PROTOCOLS;
		ev.xclient.format	= 32;
		ev.xclient.data.l[0]	= proto;
		ev.xclient.data.l[1]	= CurrentTime;
		XSendEvent(display, client->window, False, NoEventMask, &ev);
	}
	return exists;
}

void setFullscreen(SClient *client, int fullscreen)
{
	if (!client) {
		return;
	}

	if (client->isFullscreen == fullscreen) {
		return;
	}

	if (fullscreen) {
		fprintf(stderr, "Setting fullscreen for window 0x%lx\n",
			client->window);

		client->oldx	  = client->x;
		client->oldy	  = client->y;
		client->oldwidth  = client->width;
		client->oldheight = client->height;
		client->oldState  = client->isFloating;

		client->isFloating   = 1;
		client->isFullscreen = 1;

		SMonitor *monitor = &monitors[client->monitor];

		client->x      = monitor->x;
		client->y      = monitor->y;
		client->width  = monitor->width;
		client->height = monitor->height;

		XUngrabButton(display, Button1, modkey, client->window);
		XUngrabButton(display, Button3, modkey, client->window);

		XSetWindowBorderWidth(display, client->window, 0);
		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		XRaiseWindow(display, client->window);
		configureClient(client);

		XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM,
				32, PropModeReplace,
				(unsigned char *)&NET_WM_STATE_FULLSCREEN, 1);
	} else {
		fprintf(stderr, "Unsetting fullscreen for window 0x%lx\n",
			client->window);

		client->isFullscreen = 0;
		client->isFloating   = client->oldState;
		client->x	     = client->oldx;
		client->y	     = client->oldy;
		client->width	     = client->oldwidth;
		client->height	     = client->oldheight;

		XGrabButton(display, Button1, modkey, client->window, False,
			    ButtonPressMask | ButtonReleaseMask |
				ButtonMotionMask,
			    GrabModeAsync, GrabModeAsync, None, moveCursor);

		if (client->isFloating) {
			XGrabButton(display, Button3, modkey, client->window,
				    False,
				    ButtonPressMask | ButtonReleaseMask |
					ButtonMotionMask,
				    GrabModeAsync, GrabModeAsync, None,
				    resizeSECursor);
		}

		XSetWindowBorderWidth(display, client->window, borderWidth);
		XMoveResizeWindow(display, client->window, client->x, client->y,
				  client->width, client->height);
		configureClient(client);

		XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM,
				32, PropModeReplace, (unsigned char *)NULL, 0);

		SMonitor *monitor = &monitors[client->monitor];
		if (!client->isFloating &&
		    monitor->currentLayout == LAYOUT_MONOCLE) {
			fprintf(stderr, "Unfullscreening in monocle mode, "
					"setting as lastTiledClient\n");
			monitor->lastTiledClient[client->workspace] =
			    client->window;
		}
	}

	SMonitor *monitor = &monitors[client->monitor];
	arrangeClients(monitor);
	updateClientVisibility();
	updateClientAllowedActions(client);
	updateClientListStacking();
	updateBars();
}

int *getStrut(Window window)
{
	static unsigned long strut[12] = {0};
	Atom		     actual_type;
	int		     actual_format;
	unsigned long	     nitems, bytes_after;
	unsigned char	    *data = NULL;

	if (XGetWindowProperty(display, window, NET_WM_STRUT_PARTIAL, 0, 12,
			       False, XA_CARDINAL, &actual_type, &actual_format,
			       &nitems, &bytes_after, &data) == Success &&
	    data) {
		if (nitems == 12) {
			memcpy(strut, data, 12 * sizeof(unsigned long));
			XFree(data);
			fprintf(stderr,
				"Found _NET_WM_STRUT_PARTIAL: left=%lu "
				"right=%lu top=%lu bottom=%lu\n",
				strut[0], strut[1], strut[2], strut[3]);
			return (int *)strut;
		}
		XFree(data);
	}

	data = NULL;
	if (XGetWindowProperty(display, window, NET_WM_STRUT, 0, 4, False,
			       XA_CARDINAL, &actual_type, &actual_format,
			       &nitems, &bytes_after, &data) == Success &&
	    data) {
		if (nitems == 4) {
			memcpy(strut, data, 4 * sizeof(unsigned long));
			XFree(data);
			fprintf(stderr,
				"Found _NET_WM_STRUT: left=%lu right=%lu "
				"top=%lu bottom=%lu\n",
				strut[0], strut[1], strut[2], strut[3]);
			return (int *)strut;
		}
		XFree(data);
	}

	return NULL;
}

void updateWindowType(SClient *client)
{
	Atom state = getAtomProperty(client, NET_WM_STATE);
	Atom wtype = getAtomProperty(client, NET_WM_WINDOW_TYPE);

	fprintf(stderr,
		"Checking window type for 0x%lx, state=%ld, wtype=%ld\n",
		client->window, state, wtype);

	if (state == NET_WM_STATE_FULLSCREEN) {
		fprintf(stderr, "Fullscreen window detected, forcing proper "
				"position\n");
		if (!client->isFullscreen) {
			client->oldx	  = client->x;
			client->oldy	  = client->y;
			client->oldwidth  = client->width;
			client->oldheight = client->height;

			SMonitor *monitor    = &monitors[client->monitor];
			client->isFullscreen = 1;
			client->oldState     = client->isFloating;
			client->isFloating   = 1;

			client->x      = monitor->x;
			client->y      = monitor->y;
			client->width  = monitor->width;
			client->height = monitor->height;

			XUngrabButton(display, Button1, modkey, client->window);
			XUngrabButton(display, Button3, modkey, client->window);

			XSetWindowBorderWidth(display, client->window, 0);
			XChangeProperty(
			    display, client->window, NET_WM_STATE, XA_ATOM, 32,
			    PropModeReplace,
			    (unsigned char *)&NET_WM_STATE_FULLSCREEN, 1);

			XMoveResizeWindow(display, client->window, client->x,
					  client->y, client->width,
					  client->height);
			configureClient(client);
			XRaiseWindow(display, client->window);
		}
	}
	if (wtype == NET_WM_WINDOW_TYPE_DIALOG ||
	    wtype == NET_WM_WINDOW_TYPE_UTILITY) {
		fprintf(stderr, "Dialog or utility window detected, forcing "
				"floating mode\n");
		client->isFloating = 1;
	}
	if (wtype == NET_WM_WINDOW_TYPE_DOCK) {
		fprintf(stderr, "Dock window detected, setting appropriate "
				"properties\n");
		client->isDock	   = 1;
		client->isFloating = 1;
		client->noswallow  = 1;

		client->oldWorkspace = client->workspace;
		client->workspace    = DOCK_WORKSPACE;

		fprintf(stderr, "Dock window 0x%lx assigned to monitor %d\n",
			client->window, client->monitor);

		int	 *strut	  = getStrut(client->window);
		SMonitor *monitor = &monitors[client->monitor];

		if (strut) {
			if (strut[2] > 0) {
				client->y = monitor->y;
				fprintf(stderr,
					"Positioning dock at top edge: y=%d\n",
					client->y);
			} else if (strut[3] > 0) {
				client->y = monitor->y + monitor->height -
					    client->height;
				fprintf(stderr,
					"Positioning dock at bottom edge: "
					"y=%d\n",
					client->y);
			} else if (strut[0] > 0) {
				client->x = monitor->x;
				fprintf(stderr,
					"Positioning dock at left edge: x=%d\n",
					client->x);
			} else if (strut[1] > 0) {
				client->x =
				    monitor->x + monitor->width - client->width;
				fprintf(stderr,
					"Positioning dock at right edge: "
					"x=%d\n",
					client->x);
			}
		}

		XSizeHints hints;
		long	   supplied;

		if (XGetWMNormalHints(display, client->window, &hints,
				      &supplied)) {
			if (supplied & PPosition) {
				fprintf(stderr,
					"Using position hints for dock: "
					"%d,%d\n",
					hints.x, hints.y);
				client->x = hints.x;
				client->y = hints.y;
			}
		}

		XMoveWindow(display, client->window, client->x, client->y);
		XUngrabButton(display, Button1, modkey, client->window);
		XUngrabButton(display, Button3, modkey, client->window);
		XSetWindowBorderWidth(display, client->window, 0);

		for (int i = 0; i < numMonitors; i++) {
			SMonitor *m = &monitors[i];
			if (client->x < m->x + m->width &&
			    client->x + client->width > m->x &&
			    client->y < m->y + m->height &&
			    client->y + client->height > m->y) {
				fprintf(stderr,
					"Dock affects monitor %d, will arrange "
					"it\n",
					i);
				arrangeClients(&monitors[i]);
			}
		}
	}
}

void updateWMHints(SClient *client)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(display, client->window))) {
		if (client == focused && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(display, client->window, wmh);
			client->isUrgent = 0;
			updateClientUrgency(client);
		} else {
			int wasUrgent	 = client->isUrgent;
			client->isUrgent = (wmh->flags & XUrgencyHint) ? 1 : 0;

			if (wasUrgent != client->isUrgent) {
				updateClientUrgency(client);
			}
		}

		if (wmh->flags & InputHint) {
			client->neverfocus = !wmh->input;
		} else {
			client->neverfocus = 0;
		}

		XFree(wmh);
	}
}

void handleClientMessage(XEvent *event)
{
	XClientMessageEvent *cme = &event->xclient;

	SClient		    *client = findClient(cme->window);

	if (cme->message_type == NET_CURRENT_DESKTOP && cme->window == root) {
		int workspace = cme->data.l[0];
		if (workspace >= 0 && workspace < workspaceCount) {
			fprintf(stderr,
				"Received _NET_CURRENT_DESKTOP message, "
				"switching to workspace %d\n",
				workspace);
			SMonitor *currentMonitor	 = getCurrentMonitor();
			currentMonitor->currentWorkspace = workspace;
			currentWorkspace		 = workspace;

			long currentDesktop = workspace;
			XChangeProperty(display, root, NET_CURRENT_DESKTOP,
					XA_CARDINAL, 32, PropModeReplace,
					(unsigned char *)&currentDesktop, 1);

			updateDesktopViewport();
			updateClientVisibility();
			updateBars();

			SClient *clientToFocus = findVisibleClientInWorkspace(
			    currentMonitor->num, workspace);
			if (clientToFocus) {
				focusClient(clientToFocus);
			} else {
				focused = NULL;
				XSetInputFocus(display, root,
					       RevertToPointerRoot,
					       CurrentTime);
				updateBorders();
			}

			return;
		}
	}

	if (!client) {
		return;
	}

	if (cme->message_type == NET_WM_STATE) {
		if (cme->data.l[1] == (long)NET_WM_STATE_FULLSCREEN ||
		    cme->data.l[2] == (long)NET_WM_STATE_FULLSCREEN) {
			setFullscreen(
			    client,
			    (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD */ ||
			     (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
			      !client->isFullscreen)));
		}
	} else if (cme->message_type == NET_ACTIVE_WINDOW) {
		if (client != focused) {
			if (client->workspace !=
			    monitors[client->monitor].currentWorkspace) {
				client->isUrgent = 1;
				updateClientUrgency(client);
				updateBorders();
				updateBars();

				XChangeProperty(
				    display, root, NET_ACTIVE_WINDOW, XA_WINDOW,
				    32, PropModeReplace,
				    (unsigned char *)&client->window, 1);
			} else {
				focusClient(client);
			}
		}
	} else if (cme->message_type == NET_WM_DESKTOP) {
		long workspace = cme->data.l[0];

		if (workspace >= 0 && workspace < workspaceCount) {
			fprintf(stderr,
				"Received _NET_WM_DESKTOP message, moving "
				"window 0x%lx to workspace %ld\n",
				client->window, workspace);

			client->oldWorkspace = client->workspace;
			client->workspace    = workspace;
			updateClientDesktop(client);

			if (workspace !=
			    monitors[client->monitor].currentWorkspace) {
				XUnmapWindow(display, client->window);
			} else {
				XMapWindow(display, client->window);
				focusClient(client);
			}

			arrangeClients(&monitors[client->monitor]);
			updateBars();
		}
	} else if (cme->message_type == NET_CLOSE_WINDOW) {
		if (client) {
			fprintf(stderr,
				"Received _NET_CLOSE_WINDOW for 0x%lx\n",
				client->window);
			if (!sendEvent(client, WM_DELETE_WINDOW)) {
				XKillClient(display, client->window);
			}
		}
	} else if (cme->message_type == NET_WM_MOVERESIZE) {
		if (client && client->isFloating) {
			int x_root    = cme->data.l[0];
			int y_root    = cme->data.l[1];
			int direction = cme->data.l[2];

			fprintf(stderr,
				"Received _NET_WM_MOVERESIZE for 0x%lx, "
				"direction: %d\n",
				client->window, direction);

			if (direction == 8) {
				windowMovement.active	= 1;
				windowMovement.client	= client;
				windowMovement.x	= x_root;
				windowMovement.y	= y_root;
				windowMovement.wasTiled = 0;

				XRaiseWindow(display, client->window);
				XGrabPointer(display, root, False,
					     ButtonReleaseMask |
						 PointerMotionMask,
					     GrabModeAsync, GrabModeAsync, None,
					     moveCursor, CurrentTime);
			} else if (direction >= 0 && direction <= 7) {
				windowResize.active	= 1;
				windowResize.client	= client;
				windowResize.x		= x_root;
				windowResize.y		= y_root;
				windowResize.resizeType = direction;

				XRaiseWindow(display, client->window);
				XGrabPointer(display, root, False,
					     ButtonReleaseMask |
						 PointerMotionMask,
					     GrabModeAsync, GrabModeAsync, None,
					     resizeSECursor, CurrentTime);
			}
		}
	} else if (cme->message_type == NET_MOVERESIZE_WINDOW) {
		if (client) {
			fprintf(stderr,
				"Received _NET_MOVERESIZE_WINDOW for 0x%lx\n",
				client->window);

			int flags  = cme->data.l[0] & 0xFF;
			int x	   = cme->data.l[1];
			int y	   = cme->data.l[2];
			int width  = cme->data.l[3];
			int height = cme->data.l[4];

			if (flags & (1 << 0)) {
				client->x = x;
			}
			if (flags & (1 << 1)) {
				client->y = y;
			}
			if (flags & (1 << 2)) {
				client->width = width;
			}
			if (flags & (1 << 3)) {
				client->height = height;
			}

			XMoveResizeWindow(display, client->window, client->x,
					  client->y, client->width,
					  client->height);
			configureClient(client);
		}
	} else if (cme->message_type == NET_REQUEST_FRAME_EXTENTS) {
		if (client) {
			fprintf(stderr,
				"Received _NET_REQUEST_FRAME_EXTENTS for "
				"0x%lx\n",
				client->window);

			long extents[4] = {borderWidth, borderWidth,
					   borderWidth, borderWidth};

			XChangeProperty(display, client->window,
					NET_FRAME_EXTENTS, XA_CARDINAL, 32,
					PropModeReplace,
					(unsigned char *)extents, 4);
		}
	}

	restackFloatingWindows();
}

void toggleFullscreen(const char *arg)
{
	(void)arg;

	if (!focused) {
		return;
	}

	setFullscreen(focused, !focused->isFullscreen);
}

void updateSizeHints(SClient *client)
{
	XSizeHints hints;
	long	   supplied;

	client->sizeHints.valid = 0;

	if (!XGetWMNormalHints(display, client->window, &hints, &supplied)) {
		return;
	}

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

	fprintf(stderr,
		"Size hints for 0x%lx: min=%dx%d, max=%dx%d, base=%dx%d\n",
		client->window, client->sizeHints.minWidth,
		client->sizeHints.minHeight, client->sizeHints.maxWidth,
		client->sizeHints.maxHeight, client->sizeHints.baseWidth,
		client->sizeHints.baseHeight);
}

void getWindowClass(Window window, char *className, char *instanceName,
		    size_t bufSize)
{
	XClassHint classHint;

	className[0]	= '\0';
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

int applyRules(SClient *client)
{
	char className[256]    = {0};
	char instanceName[256] = {0};

	getWindowClass(client->window, className, instanceName,
		       sizeof(className));

	XTextProperty textprop;
	char	     *windowTitle = NULL;
	if (XGetWMName(display, client->window, &textprop) && textprop.value &&
	    textprop.nitems) {
		windowTitle = (char *)textprop.value;
	}

	int rulesApplied = 0;

	for (size_t i = 0; i < rulesCount; i++) {
		const SWindowRule *rule = &rules[i];

		if (rule->className && strcmp(rule->className, "*") != 0 &&
		    (!className[0] ||
		     strcasecmp(rule->className, className) != 0)) {
			continue;
		}

		if (rule->instanceName &&
		    (!instanceName[0] ||
		     strcasecmp(rule->instanceName, instanceName) != 0)) {
			continue;
		}

		if (rule->title &&
		    (!windowTitle || !strstr(windowTitle, rule->title))) {
			continue;
		}

		if (rule->isFloating != -1) {
			client->isFloating = rule->isFloating;
		}

		if (rule->workspace != -1) {
			client->workspace = rule->workspace;
		}

		if (rule->monitor != -1 && rule->monitor < numMonitors) {
			client->monitor = rule->monitor;
		}

		SMonitor *mon	      = &monitors[client->monitor];
		int	  sizeChanged = 0;

		if (rule->width > 0) {
			client->width = rule->width;
			sizeChanged   = 1;
		}

		if (rule->height > 0) {
			client->height = rule->height;
			sizeChanged    = 1;
		}

		if (rule->swallowing != -1) {
			client->isSwallowing = rule->swallowing;
			fprintf(stderr,
				"Setting isSwallowing=%d for window 0x%lx "
				"(rule matched)\n",
				client->isSwallowing, client->window);
		}

		if (rule->noswallow != -1) {
			client->noswallow = rule->noswallow;
			fprintf(stderr,
				"Setting noswallow=%d for window 0x%lx (rule "
				"matched)\n",
				client->noswallow, client->window);
		}

		if (sizeChanged && client->isFloating) {
			client->x = mon->x + (mon->width - client->width) / 2;
			client->y = mon->y + (mon->height - client->height) / 2;

			if (client->y < mon->y + barHeight) {
				client->y = mon->y + barHeight;
			}
		}

		fprintf(stderr,
			"Applied rule for window class=%s instance=%s "
			"title=%s\n",
			className, instanceName,
			windowTitle ? windowTitle : "(null)");

		rulesApplied = 1;
	}

	if (textprop.value) {
		XFree(textprop.value);
	}

	return rulesApplied;
}

void focusMonitor(const char *arg)
{
	if (!arg) {
		return;
	}

	SMonitor *currentMonitor = getCurrentMonitor();
	int	  currentNum	 = currentMonitor->num;
	int	  targetMonitor	 = currentNum;

	if (strcmp(arg, "right") == 0) {
		targetMonitor = (currentNum + 1) % numMonitors;
	} else if (strcmp(arg, "left") == 0) {
		targetMonitor = (currentNum - 1 + numMonitors) % numMonitors;
	} else {
		int monitorNum = atoi(arg);
		if (monitorNum >= 0 && monitorNum < numMonitors) {
			targetMonitor = monitorNum;
		}
	}

	if (targetMonitor == currentNum) {
		return;
	}

	SMonitor *monitor = &monitors[targetMonitor];

	int	  centerX = monitor->x + monitor->width / 2;
	int	  centerY = monitor->y + monitor->height / 2;

	if (!no_warps) {
		XWarpPointer(display, None, root, 0, 0, 0, 0, centerX, centerY);
		lastCursorWarp = 1;
		gettimeofday(&lastWindowOperation, NULL);
	} else {
		forcedMonitor = targetMonitor;
	}

	SClient *clientToFocus = findVisibleClientInWorkspace(
	    targetMonitor, monitor->currentWorkspace);

	if (clientToFocus) {
		focusClient(clientToFocus);
	} else {
		if (focused) {
			if (focused->monitor != targetMonitor) {
				XSetInputFocus(display, root,
					       RevertToPointerRoot,
					       CurrentTime);
				focused = NULL;
				updateBorders();
			}
		} else {
			XSetInputFocus(display, root, RevertToPointerRoot,
				       CurrentTime);
		}
	}

	currentWorkspace = monitor->currentWorkspace;

	updateBars();

	long currentDesktop = currentWorkspace;
	XChangeProperty(display, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&currentDesktop, 1);
	updateDesktopViewport();
}

void toggleBar(const char *arg)
{
	(void)arg;
	SMonitor *currentMonitor = getCurrentMonitor();

	if (hasDocksOnMonitor(currentMonitor->num)) {
		fprintf(stderr, "Bar toggling disabled - docks are active on "
				"this monitor\n");
		if (barVisible) {
			barVisible = 0;
			showHideBars(0);
			updateClientPositionsForBar();

			arrangeClients(currentMonitor);
		}
		return;
	}

	barVisible = !barVisible;
	showHideBars(barVisible);
	updateClientPositionsForBar();
	arrangeClients(currentMonitor);
}

void cycleLayouts(const char *arg)
{
	(void)arg;

	SMonitor *currentMonitor = getCurrentMonitor();
	if (!currentMonitor) {
		return;
	}

	ELayout newLayout = (currentMonitor->currentLayout == LAYOUT_TILED)
				? LAYOUT_MONOCLE
				: LAYOUT_TILED;

	for (int i = 0; i < numMonitors; i++) {
		for (int ws = 0; ws < workspaceCount; ws++) {
			monitors[i].workspaceLayouts[ws] = newLayout;
		}
		monitors[i].currentLayout = newLayout;
		arrangeClients(&monitors[i]);
	}

	updateClientVisibility();
	updateBorders();
}

void updateMasterFactorsForAllMonitors(void)
{
	for (int i = 0; i < numMonitors; i++) {
		for (int ws = 0; ws < workspaceCount; ws++) {
			int hasWindows = 0;
			for (SClient *c = clients; c; c = c->next) {
				if (c->monitor == i && c->workspace == ws &&
				    !c->isFloating) {
					hasWindows = 1;
					break;
				}
			}

			if (hasWindows) {
				monitors[i].masterFactors[ws] =
				    defaultMasterFactor;
			}
		}
	}
}

void tileAllMonitors(void)
{
	updateMasterFactorsForAllMonitors();

	ELayout configLayout = strcasecmp(defaultLayout, "monocle") == 0
				   ? LAYOUT_MONOCLE
				   : LAYOUT_TILED;

	for (int i = 0; i < numMonitors; i++) {
		for (int ws = 0; ws < workspaceCount; ws++) {
			monitors[i].workspaceLayouts[ws] = configLayout;
		}
		monitors[i].currentLayout = configLayout;
		arrangeClients(&monitors[i]);
	}
}

int getWindowPID(Window window)
{
	Atom	       actual_type;
	int	       actual_format;
	unsigned long  nitems, bytes_after;
	unsigned char *prop = NULL;
	int	       pid  = -1;

	static Atom    atom_pid = None;
	if (atom_pid == None) {
		atom_pid = XInternAtom(display, "_NET_WM_PID", False);
		fprintf(stderr, "Initialized _NET_WM_PID atom\n");
	}

	fprintf(stderr, "Getting PID for window 0x%lx\n", window);

	if (XGetWindowProperty(display, window, atom_pid, 0, 1, False,
			       XA_CARDINAL, &actual_type, &actual_format,
			       &nitems, &bytes_after, &prop) == Success) {
		if (prop && actual_type == XA_CARDINAL && actual_format == 32 &&
		    nitems == 1) {
			pid = *((int *)prop);
			fprintf(stderr, "Window 0x%lx has PID %d\n", window,
				pid);
		} else {
			fprintf(stderr,
				"Window 0x%lx has no PID property (type=%ld, "
				"format=%d, nitems=%ld)\n",
				window, actual_type, actual_format, nitems);
		}
		if (prop) {
			XFree(prop);
		}
	} else {
		fprintf(stderr, "Failed to get PID property for window 0x%lx\n",
			window);
	}

	return pid;
}

int isChildProcess(int parentPid, int childPid)
{
	if (parentPid <= 0 || childPid <= 0) {
		fprintf(stderr,
			"Invalid PIDs for child process check: parent=%d, "
			"child=%d\n",
			parentPid, childPid);
		return 0;
	}

	char parentPath[256];
	char buffer[256];

	snprintf(parentPath, sizeof(parentPath), "/proc/%d/task/%d/children",
		 parentPid, parentPid);
	fprintf(stderr,
		"Checking parent-child relationship: %d -> %d (path: %s)\n",
		parentPid, childPid, parentPath);

	FILE *f = fopen(parentPath, "r");
	if (!f) {
		fprintf(stderr, "Failed to open %s: %s\n", parentPath,
			strerror(errno));
		return 0;
	}

	if (!fgets(buffer, sizeof(buffer), f)) {
		fprintf(stderr, "No children found for PID %d\n", parentPid);
		fclose(f);
		return 0;
	}

	fprintf(stderr, "Children of PID %d: %s\n", parentPid, buffer);

	char *token = strtok(buffer, " ");
	while (token) {
		int pid = atoi(token);
		if (pid == childPid) {
			fclose(f);
			fprintf(stderr,
				"Found direct child PID match: %d is "
				"child of %d\n",
				childPid, parentPid);
			return 1;
		}
		token = strtok(NULL, " ");
	}

	fseek(f, 0, SEEK_SET);
	if (!fgets(buffer, sizeof(buffer), f)) {
		fclose(f);
		return 0;
	}

	char *bufferCopy = strdup(buffer);
	if (!bufferCopy) {
		fclose(f);
		return 0;
	}

	token = strtok(bufferCopy, " ");
	while (token) {
		int pid = atoi(token);
		fclose(f);

		if (pid <= 0 || pid == parentPid) {
			f = fopen(parentPath, "r");
			if (!f) {
				fprintf(stderr, "Failed to reopen %s\n",
					parentPath);
				free(bufferCopy);
				return 0;
			}
			token = strtok(NULL, " ");
			continue;
		}

		fprintf(stderr,
			"Checking indirect child relationship through: %d\n",
			pid);
		if (isChildProcess(pid, childPid)) {
			fprintf(stderr,
				"Found indirect child relationship: %d -> %d "
				"-> %d\n",
				parentPid, pid, childPid);
			free(bufferCopy);
			return 1;
		}

		f = fopen(parentPath, "r");
		if (!f) {
			fprintf(stderr, "Failed to reopen %s\n", parentPath);
			free(bufferCopy);
			return 0;
		}

		token = strtok(NULL, " ");
	}

	free(bufferCopy);
	fclose(f);
	return 0;
}

void trySwallowClient(SClient *client)
{
	if (!client) {
		fprintf(stderr, "trySwallowClient: client is NULL\n");
		return;
	}

	if (client->pid <= 0) {
		fprintf(stderr, "trySwallowClient: client PID is invalid: %d\n",
			client->pid);
		return;
	}

	if (client->noswallow) {
		fprintf(stderr,
			"Skipping swallow for client 0x%lx - has noswallow "
			"flag\n",
			client->window);
		return;
	}

	fprintf(stderr, "Trying to swallow client 0x%lx with PID %d\n",
		client->window, client->pid);

	for (SClient *c = clients; c; c = c->next) {
		if (c == client) {
			continue;
		}

		if (c->swallowed) {
			fprintf(stderr,
				"Skipping client 0x%lx - already swallowing "
				"another window\n",
				c->window);
			continue;
		}

		if (!c->isSwallowing) {
			fprintf(stderr,
				"Skipping client 0x%lx - swallowing not "
				"enabled (isSwallowing=%d)\n",
				c->window, c->isSwallowing);
			continue;
		}

		fprintf(stderr,
			"Checking if client 0x%lx (PID %d) can swallow 0x%lx "
			"(PID %d)\n",
			c->window, c->pid, client->window, client->pid);

		if (c->monitor != client->monitor ||
		    c->workspace != client->workspace) {
			fprintf(stderr,
				"Skipping client 0x%lx - different "
				"monitor/workspace\n",
				c->window);
			continue;
		}

		if (isChildProcess(c->pid, client->pid)) {
			fprintf(stderr,
				"Swallowing client 0x%lx (PID %d) by 0x%lx "
				"(PID %d)\n",
				client->window, client->pid, c->window, c->pid);

			int	 wasFloating	= client->isFloating;
			int	 originalX	= client->x;
			int	 originalY	= client->y;
			int	 originalWidth	= client->width;
			int	 originalHeight = client->height;

			SClient *prevParent = NULL;
			SClient *curr	    = clients;
			while (curr && curr != c) {
				prevParent = curr;
				curr	   = curr->next;
			}

			SClient *prevChild = NULL;
			curr		   = clients;
			while (curr && curr != client) {
				prevChild = curr;
				curr	  = curr->next;
			}

			if (prevChild) {
				prevChild->next = client->next;
			} else {
				clients = client->next;
			}

			if (prevParent) {
				client->next	 = prevParent->next;
				prevParent->next = client;
			} else {
				client->next = clients;
				clients	     = client;
			}

			c->swallowed	    = client;
			client->swallowedBy = c;

			int parentX	     = c->x;
			int parentY	     = c->y;
			int parentWidth	     = c->width;
			int parentHeight     = c->height;
			int parentIsFloating = c->isFloating;

			fprintf(stderr,
				"Parent window geometry being inherited: "
				"%dx%d at %d,%d (floating: %d)\n",
				parentWidth, parentHeight, parentX, parentY,
				parentIsFloating);

			unmapSwallowedClient(c);

			if (wasFloating) {
				fprintf(stderr,
					"Child was already floating, "
					"preserving geometry: "
					"%dx%d at %d,%d\n",
					originalWidth, originalHeight,
					originalX, originalY);
				client->x      = originalX;
				client->y      = originalY;
				client->width  = originalWidth;
				client->height = originalHeight;
			} else {
				fprintf(stderr,
					"Child inheriting parent geometry: "
					"%dx%d at %d,%d (floating: %d)\n",
					parentWidth, parentHeight, parentX,
					parentY, parentIsFloating);
				client->isFloating = parentIsFloating;
				client->x	   = parentX;
				client->y	   = parentY;
				client->width	   = parentWidth;
				client->height	   = parentHeight;
			}

			if (client->isFloating) {
				XGrabButton(display, Button3, modkey,
					    client->window, False,
					    ButtonPressMask |
						ButtonReleaseMask |
						ButtonMotionMask,
					    GrabModeAsync, GrabModeAsync, None,
					    resizeSECursor);

				XMoveResizeWindow(display, client->window,
						  client->x, client->y,
						  client->width,
						  client->height);

				fprintf(stderr, "Applying resize for swallowed "
						"window\n");
				int tempWidth  = client->width - 1;
				int tempHeight = client->height - 1;

				XResizeWindow(display, client->window,
					      tempWidth, tempHeight);
				XSync(display, False);

				XResizeWindow(display, client->window,
					      client->width, client->height);
				XSync(display, False);

				XRaiseWindow(display, client->window);

				fprintf(stderr,
					"Applied floating geometry to child: "
					"%dx%d at %d,%d\n",
					client->width, client->height,
					client->x, client->y);
			}

			configureClient(client);

			arrangeClients(&monitors[client->monitor]);
			focusClient(client);
			break;
		} else {
			fprintf(stderr, "Not a child process: %d -> %d\n",
				c->pid, client->pid);
		}
	}
}

void unmapSwallowedClient(SClient *swallowed)
{
	if (!swallowed) {
		return;
	}

	fprintf(stderr, "Moving swallowed client 0x%lx to hidden workspace\n",
		swallowed->window);

	swallowed->oldWorkspace = swallowed->workspace;

	int monitor = swallowed->monitor;

	swallowed->workspace = INT_MAX;
	updateClientDesktop(swallowed);

	updateClientVisibility();

	arrangeClients(&monitors[monitor]);
}

void remapSwallowedClient(SClient *client)
{
	if (!client || !client->swallowedBy) {
		return;
	}

	SClient *parent = client->swallowedBy;
	fprintf(stderr,
		"Restoring previously swallowed client 0x%lx to workspace %d\n",
		parent->window, parent->oldWorkspace);

	parent->swallowed   = NULL;
	client->swallowedBy = NULL;

	parent->workspace = parent->oldWorkspace;
	updateClientDesktop(parent);

	XWindowAttributes wa;
	if (XGetWindowAttributes(display, parent->window, &wa)) {
		if (parent->workspace ==
		    monitors[parent->monitor].currentWorkspace) {
			XMapWindow(display, parent->window);
			XSync(display, False);
			focusClient(parent);
			XRaiseWindow(display, parent->window);
		}
	} else {
		fprintf(stderr,
			"Parent window 0x%lx no longer exists, skipping "
			"focus\n",
			parent->window);
	}

	arrangeClients(&monitors[parent->monitor]);
	updateClientVisibility();
}

void handleScreenChange(XEvent *event)
{
	fprintf(stderr, "Screen configuration changed, updating monitors\n");

	XRRUpdateConfiguration(event);

	updateMonitors();

	if (barWindows) {
		for (int i = 0; i < numMonitors; i++) {
			if (barWindows[i] != 0) {
				XDestroyWindow(display, barWindows[i]);
				barWindows[i] = 0;
			}
		}
		free(barWindows);
		barWindows = NULL;
	}

	createBars();
	updateStatus();

	for (SClient *c = clients; c; c = c->next) {
		if (c->monitor >= numMonitors) {
			c->monitor = 0;
		}

		SMonitor *mon = &monitors[c->monitor];
		if (c->x < mon->x || c->x >= mon->x + mon->width ||
		    c->y < mon->y || c->y >= mon->y + mon->height) {
			c->x = mon->x + (mon->width - c->width) / 2;
			c->y = mon->y + (mon->height - c->height) / 2;
			if (c->y < mon->y + barHeight) {
				c->y = mon->y + barHeight;
			}
			XMoveWindow(display, c->window, c->x, c->y);
		}
	}

	updateClientPositionsForBar();

	for (int i = 0; i < numMonitors; i++) {
		arrangeClients(&monitors[i]);
	}

	updateClientVisibility();
	updateBars();
	updateDesktopViewport();
}

int getDockHeight(int monitorNum, int workspace)
{
	int	  dockHeight = 0;
	SMonitor *m	     = &monitors[monitorNum];

	for (SClient *c = clients; c; c = c->next) {
		if (c->isDock) {
			if (c->x < m->x + m->width && c->x + c->width > m->x &&
			    c->y < m->y + m->height &&
			    c->y + c->height > m->y) {
				dockHeight += c->height;
				fprintf(stderr,
					"Found dock %lx overlapping monitor "
					"%d, height %d, total now %d\n",
					c->window, monitorNum, c->height,
					dockHeight);
			}
		}
	}

	fprintf(stderr, "Total dock height for monitor %d workspace %d: %d\n",
		monitorNum, workspace, dockHeight);
	return dockHeight;
}

int getDockPosition(int monitorNum)
{
	SMonitor *m = &monitors[monitorNum];

	for (SClient *c = clients; c; c = c->next) {
		if (c->isDock) {
			if (c->x < m->x + m->width && c->x + c->width > m->x &&
			    c->y < m->y + m->height &&
			    c->y + c->height > m->y) {
				int *strut = getStrut(c->window);
				if (strut) {
					if (strut[3] > 0) {
						return 1;
					} else if (strut[2] > 0) {
						return 0;
					}
				}

				if (c->y > m->y + m->height / 2) {
					return 1;
				}
			}
		}
	}

	return 0;
}

void updateDesktopViewport()
{
	if (!monitors || numMonitors <= 0 || workspaceCount <= 0) {
		fprintf(stderr, "Cannot update desktop viewport: no monitors "
				"or invalid workspace count\n");
		return;
	}

	long data[workspaceCount * 2];
	int  idx = 0;

	for (int ws = 0; ws < workspaceCount; ws++) {
		int monitorFound = 0;

		for (int m = 0; m < numMonitors; m++) {
			if (monitors[m].currentWorkspace == ws) {
				data[idx++]  = monitors[m].x;
				data[idx++]  = monitors[m].y;
				monitorFound = 1;
				break;
			}
		}

		if (!monitorFound) {
			data[idx++] = monitors[0].x;
			data[idx++] = monitors[0].y;
		}
	}

	XChangeProperty(display, root, NET_DESKTOP_VIEWPORT, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)data, idx);

	fprintf(stderr,
		"Updated desktop viewport information for %d workspaces\n",
		workspaceCount);
}

void updateClientDesktop(SClient *client)
{
	if (!client) {
		return;
	}

	long desktop = client->workspace;

	if (desktop == DOCK_WORKSPACE || desktop == INT_MAX) {
		desktop = 0;
	}

	XChangeProperty(display, client->window, NET_WM_DESKTOP, XA_CARDINAL,
			32, PropModeReplace, (unsigned char *)&desktop, 1);

	fprintf(stderr, "Updated NET_WM_DESKTOP to %ld for window 0x%lx\n",
		desktop, client->window);
}

void updateClientAllowedActions(SClient *client)
{
	if (!client) {
		return;
	}

	Atom actions[10];
	int  count = 0;

	actions[count++] = NET_WM_ACTION_CLOSE;
	actions[count++] = NET_WM_ACTION_MOVE;
	actions[count++] = NET_WM_ACTION_CHANGE_DESKTOP;

	if (!client->isFullscreen) {
		actions[count++] = NET_WM_ACTION_FULLSCREEN;
	}

	if (!client->isDock) {
		actions[count++] = NET_WM_ACTION_RESIZE;

		if (!client->isFullscreen) {
			actions[count++] = NET_WM_ACTION_MAXIMIZE_VERT;
			actions[count++] = NET_WM_ACTION_MAXIMIZE_HORZ;
		}
	}

	XChangeProperty(display, client->window, NET_WM_ALLOWED_ACTIONS,
			XA_ATOM, 32, PropModeReplace, (unsigned char *)actions,
			count);

	fprintf(stderr,
		"Updated _NET_WM_ALLOWED_ACTIONS for window 0x%lx with %d "
		"actions\n",
		client->window, count);
}

void updateDesktopNames()
{
	if (workspaceCount <= 0) {
		return;
	}

	char *names[workspaceCount];
	char  buffer[workspaceCount][32];
	int   totalSize = 0;

	for (int i = 0; i < workspaceCount; i++) {
		snprintf(buffer[i], sizeof(buffer[i]), "%d", i + 1);
		names[i] = buffer[i];
		totalSize += strlen(names[i]) + 1;
	}

	char *nameBuffer = malloc(totalSize);
	if (!nameBuffer) {
		fprintf(stderr, "Failed to allocate memory for desktop "
				"names\n");
		return;
	}

	char *ptr = nameBuffer;
	for (int i = 0; i < workspaceCount; i++) {
		strcpy(ptr, names[i]);
		ptr += strlen(names[i]) + 1;
	}

	XChangeProperty(display, root, NET_DESKTOP_NAMES, UTF8_STRING, 8,
			PropModeReplace, (unsigned char *)nameBuffer,
			totalSize);

	free(nameBuffer);
	fprintf(stderr, "Updated _NET_DESKTOP_NAMES with %d workspaces\n",
		workspaceCount);
}

void updateClientUrgency(SClient *client)
{
	if (!client) {
		return;
	}

	Atom	       atoms[32];
	int	       count = 0;

	Atom	       actual_type;
	int	       actual_format;
	unsigned long  nitems, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(display, client->window, NET_WM_STATE, 0, 1024,
			       False, XA_ATOM, &actual_type, &actual_format,
			       &nitems, &bytes_after, &data) == Success &&
	    data) {
		if (actual_type == XA_ATOM && actual_format == 32) {
			Atom *states = (Atom *)data;
			for (unsigned long i = 0; i < nitems; i++) {
				if (states[i] !=
				    NET_WM_STATE_DEMANDS_ATTENTION) {
					atoms[count++] = states[i];
				}
			}
		}
		XFree(data);
	}

	if (client->isUrgent) {
		atoms[count++] = NET_WM_STATE_DEMANDS_ATTENTION;
	}

	XChangeProperty(display, client->window, NET_WM_STATE, XA_ATOM, 32,
			PropModeReplace, (unsigned char *)atoms, count);

	XWMHints *wmh = XGetWMHints(display, client->window);
	if (wmh) {
		if (client->isUrgent) {
			wmh->flags |= XUrgencyHint;
		} else {
			wmh->flags &= ~XUrgencyHint;
		}
		XSetWMHints(display, client->window, wmh);
		XFree(wmh);
	} else if (client->isUrgent) {
		wmh = XAllocWMHints();
		if (wmh) {
			wmh->flags = XUrgencyHint;
			XSetWMHints(display, client->window, wmh);
			XFree(wmh);
		}
	}

	fprintf(stderr, "Updated urgency state for window 0x%lx: urgent=%d\n",
		client->window, client->isUrgent);

	updateClientListStacking();
}

int main(int argc, char *argv[])
{
	if (argc > 1) {
		if (strcmp(argv[1], "validate") == 0) {
			SConfigErrors errors;
			memset(&errors, 0, sizeof(SConfigErrors));

			int result = validateConfig(&errors);
			printConfigErrors(&errors);

			return result ? 0 : 1;
		} else if (strcmp(argv[1], "reload") == 0) {
			int result = ipcSendCommand(IPC_COMMAND_RELOAD, NULL);
			if (result != 0) {
				fprintf(stderr, "Failed to send reload command "
						"to running banana instance\n");
				return 1;
			}
			return 0;
		} else {
			fprintf(stderr, "banana: unknown command '%s'\n",
				argv[1]);
			fprintf(stderr, "Usage: banana [validate|reload]\n");
			return 1;
		}
	}

	signal(SIGCHLD, SIG_IGN);

	setup();
	scanExistingWindows();
	updateClientList();

	runAutostart();

	run();

	cleanup();

	return 0;
}