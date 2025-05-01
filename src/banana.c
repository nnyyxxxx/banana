#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xinerama.h>
#include <xcb/xproto.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include "banana.h"
#include "config.h"
#include "bar.h"

extern char*      safeStrdup(const char* s);

xcb_connection_t* connection;
xcb_screen_t*     screen;
xcb_window_t      root;
SClient*          clients     = NULL;
SClient*          focused     = NULL;
SMonitor*         monitors    = NULL;
int               numMonitors = 0;
xcb_cursor_t      normalCursor;
xcb_cursor_t      moveCursor;
xcb_cursor_t      resizeSECursor;
xcb_cursor_t      resizeSWCursor;
xcb_cursor_t      resizeNECursor;
xcb_cursor_t      resizeNWCursor;
SWindowMovement   windowMovement        = {0, 0, NULL, 0, 0};
SWindowResize     windowResize          = {0, 0, NULL, 0, 0};
int               currentWorkspace      = 0;
xcb_window_t      lastMappedWindow      = 0;
int               ignoreNextEnterNotify = 0;

xcb_atom_t        WM_PROTOCOLS;
xcb_atom_t        WM_DELETE_WINDOW;
xcb_atom_t        WM_STATE;
xcb_atom_t        WM_TAKE_FOCUS;

xcb_atom_t        NET_SUPPORTED;
xcb_atom_t        NET_WM_NAME;
xcb_atom_t        NET_SUPPORTING_WM_CHECK;
xcb_atom_t        NET_CLIENT_LIST;
xcb_atom_t        NET_NUMBER_OF_DESKTOPS;
xcb_atom_t        NET_CURRENT_DESKTOP;
xcb_atom_t        NET_WM_STATE;
xcb_atom_t        NET_WM_STATE_FULLSCREEN;
xcb_atom_t        NET_WM_WINDOW_TYPE;
xcb_atom_t        NET_WM_WINDOW_TYPE_DIALOG;
xcb_atom_t        NET_WM_WINDOW_TYPE_UTILITY;
xcb_atom_t        NET_ACTIVE_WINDOW;
xcb_atom_t        UTF8_STRING;
xcb_window_t      wmcheckwin;

int               workspaceSwitchActive = 0;

int               xerrorHandler(xcb_connection_t* dpy, xcb_generic_error_t* ee) {
    (void)dpy;

    if (ee->error_code == 3 /* XCB_BAD_WINDOW */ || (ee->major_code == XCB_SET_INPUT_FOCUS && ee->error_code == 8 /* XCB_BAD_MATCH */) ||
        (ee->major_code == XCB_CONFIGURE_WINDOW && ee->error_code == 8 /* XCB_BAD_MATCH */) || (ee->major_code == XCB_GET_GEOMETRY && ee->error_code == 9 /* XCB_BAD_DRAWABLE */)) {
        return 0;
    }

    char errorText[256];
    snprintf(errorText, sizeof(errorText), "XCB error %d", ee->error_code);
    fprintf(stderr, "banana: X error: %s (0x%x) request %d\n", errorText, ee->error_code, ee->major_code);
    return 0;
}

int otherWmRunningHandler(xcb_connection_t* dpy, xcb_generic_error_t* ee) {
    if (ee->error_code == 10 /* XCB_BAD_ACCESS */ && ee->major_code == XCB_CHANGE_WINDOW_ATTRIBUTES) {
        fprintf(stderr, "banana: another window manager is already running\n");
        exit(1);
    }
    return xerrorHandler(dpy, ee);
}

void checkOtherWM() {
    xcb_generic_error_t* error;

    uint32_t             mask   = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    xcb_void_cookie_t    cookie = xcb_change_window_attributes_checked(connection, root, XCB_CW_EVENT_MASK, &mask);

    error = xcb_request_check(connection, cookie);
    if (error) {
        if (error->error_code == 10 /* XCB_ACCESS */) {
            fprintf(stderr, "banana: another window manager is already running\n");
            exit(1);
        }
        free(error);
    }

    mask = 0;
    xcb_change_window_attributes(connection, root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(connection);
}

static void (*eventHandlers[256])(xcb_generic_event_t*) = {0};

void setupEventHandlers() {
    eventHandlers[XCB_KEY_PRESS]         = handleKeyPress;
    eventHandlers[XCB_BUTTON_PRESS]      = handleButtonPress;
    eventHandlers[XCB_BUTTON_RELEASE]    = handleButtonRelease;
    eventHandlers[XCB_MOTION_NOTIFY]     = handleMotionNotify;
    eventHandlers[XCB_ENTER_NOTIFY]      = handleEnterNotify;
    eventHandlers[XCB_MAP_REQUEST]       = handleMapRequest;
    eventHandlers[XCB_CONFIGURE_REQUEST] = handleConfigureRequest;
    eventHandlers[XCB_UNMAP_NOTIFY]      = handleUnmapNotify;
    eventHandlers[XCB_DESTROY_NOTIFY]    = handleDestroyNotify;
    eventHandlers[XCB_EXPOSE]            = handleExpose;
    eventHandlers[XCB_PROPERTY_NOTIFY]   = handlePropertyNotify;
    eventHandlers[XCB_CLIENT_MESSAGE]    = handleClientMessage;
}

void scanExistingWindows() {
    xcb_query_tree_cookie_t cookie = xcb_query_tree(connection, root);
    xcb_query_tree_reply_t* reply  = xcb_query_tree_reply(connection, cookie, NULL);

    if (reply) {
        xcb_window_t* children    = xcb_query_tree_children(reply);
        uint32_t      numChildren = xcb_query_tree_children_length(reply);

        for (uint32_t i = 0; i < numChildren; i++) {
            xcb_get_window_attributes_cookie_t attr_cookie = xcb_get_window_attributes(connection, children[i]);
            xcb_get_window_attributes_reply_t* wa          = xcb_get_window_attributes_reply(connection, attr_cookie, NULL);

            if (wa && !wa->override_redirect && wa->map_state == XCB_MAP_STATE_VIEWABLE)
                manageClient(children[i]);

            free(wa);
        }

        free(reply);
    }
}

void setupEWMH() {
    xcb_intern_atom_cookie_t cookies[14];
    cookies[0]  = xcb_intern_atom(connection, 0, strlen("_NET_SUPPORTED"), "_NET_SUPPORTED");
    cookies[1]  = xcb_intern_atom(connection, 0, strlen("_NET_WM_NAME"), "_NET_WM_NAME");
    cookies[2]  = xcb_intern_atom(connection, 0, strlen("_NET_SUPPORTING_WM_CHECK"), "_NET_SUPPORTING_WM_CHECK");
    cookies[3]  = xcb_intern_atom(connection, 0, strlen("_NET_CLIENT_LIST"), "_NET_CLIENT_LIST");
    cookies[4]  = xcb_intern_atom(connection, 0, strlen("_NET_NUMBER_OF_DESKTOPS"), "_NET_NUMBER_OF_DESKTOPS");
    cookies[5]  = xcb_intern_atom(connection, 0, strlen("_NET_CURRENT_DESKTOP"), "_NET_CURRENT_DESKTOP");
    cookies[6]  = xcb_intern_atom(connection, 0, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
    cookies[7]  = xcb_intern_atom(connection, 0, strlen("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN");
    cookies[8]  = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE"), "_NET_WM_WINDOW_TYPE");
    cookies[9]  = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE_DIALOG"), "_NET_WM_WINDOW_TYPE_DIALOG");
    cookies[10] = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE_UTILITY"), "_NET_WM_WINDOW_TYPE_UTILITY");
    cookies[11] = xcb_intern_atom(connection, 0, strlen("_NET_ACTIVE_WINDOW"), "_NET_ACTIVE_WINDOW");
    cookies[12] = xcb_intern_atom(connection, 0, strlen("UTF8_STRING"), "UTF8_STRING");

    xcb_intern_atom_reply_t* reply;

    reply         = xcb_intern_atom_reply(connection, cookies[0], NULL);
    NET_SUPPORTED = reply->atom;
    free(reply);

    reply       = xcb_intern_atom_reply(connection, cookies[1], NULL);
    NET_WM_NAME = reply->atom;
    free(reply);

    reply                   = xcb_intern_atom_reply(connection, cookies[2], NULL);
    NET_SUPPORTING_WM_CHECK = reply->atom;
    free(reply);

    reply           = xcb_intern_atom_reply(connection, cookies[3], NULL);
    NET_CLIENT_LIST = reply->atom;
    free(reply);

    reply                  = xcb_intern_atom_reply(connection, cookies[4], NULL);
    NET_NUMBER_OF_DESKTOPS = reply->atom;
    free(reply);

    reply               = xcb_intern_atom_reply(connection, cookies[5], NULL);
    NET_CURRENT_DESKTOP = reply->atom;
    free(reply);

    reply        = xcb_intern_atom_reply(connection, cookies[6], NULL);
    NET_WM_STATE = reply->atom;
    free(reply);

    reply                   = xcb_intern_atom_reply(connection, cookies[7], NULL);
    NET_WM_STATE_FULLSCREEN = reply->atom;
    free(reply);

    reply              = xcb_intern_atom_reply(connection, cookies[8], NULL);
    NET_WM_WINDOW_TYPE = reply->atom;
    free(reply);

    reply                     = xcb_intern_atom_reply(connection, cookies[9], NULL);
    NET_WM_WINDOW_TYPE_DIALOG = reply->atom;
    free(reply);

    reply                      = xcb_intern_atom_reply(connection, cookies[10], NULL);
    NET_WM_WINDOW_TYPE_UTILITY = reply->atom;
    free(reply);

    reply             = xcb_intern_atom_reply(connection, cookies[11], NULL);
    NET_ACTIVE_WINDOW = reply->atom;
    free(reply);

    reply       = xcb_intern_atom_reply(connection, cookies[12], NULL);
    UTF8_STRING = reply->atom;
    free(reply);

    wmcheckwin = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, wmcheckwin, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, 32, 1, &wmcheckwin);

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, wmcheckwin, NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, 32, 1, &wmcheckwin);

    const char* name = "banana";
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, wmcheckwin, NET_WM_NAME, UTF8_STRING, 8, 6, name);

    xcb_atom_t supported[] = {NET_SUPPORTED,
                              NET_WM_NAME,
                              NET_SUPPORTING_WM_CHECK,
                              NET_CLIENT_LIST,
                              NET_NUMBER_OF_DESKTOPS,
                              NET_CURRENT_DESKTOP,
                              NET_WM_STATE,
                              NET_WM_STATE_FULLSCREEN,
                              NET_WM_WINDOW_TYPE,
                              NET_WM_WINDOW_TYPE_DIALOG,
                              NET_WM_WINDOW_TYPE_UTILITY,
                              NET_ACTIVE_WINDOW};

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_SUPPORTED, XCB_ATOM_ATOM, 32, sizeof(supported) / sizeof(xcb_atom_t), supported);

    uint32_t numDesktops = workspaceCount;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_NUMBER_OF_DESKTOPS, XCB_ATOM_CARDINAL, 32, 1, &numDesktops);

    uint32_t currentDesktop = currentWorkspace;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, &currentDesktop);

    updateClientList();
}

void setup() {
    connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "banana: cannot open display\n");
        exit(1);
    }

    screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    root   = screen->root;

    checkOtherWM();

    xcb_intern_atom_cookie_t cookie_protocols = xcb_intern_atom(connection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t cookie_delete    = xcb_intern_atom(connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
    xcb_intern_atom_cookie_t cookie_state     = xcb_intern_atom(connection, 0, strlen("WM_STATE"), "WM_STATE");
    xcb_intern_atom_cookie_t cookie_focus     = xcb_intern_atom(connection, 0, strlen("WM_TAKE_FOCUS"), "WM_TAKE_FOCUS");

    xcb_intern_atom_reply_t* reply;

    reply        = xcb_intern_atom_reply(connection, cookie_protocols, NULL);
    WM_PROTOCOLS = reply->atom;
    free(reply);

    reply            = xcb_intern_atom_reply(connection, cookie_delete, NULL);
    WM_DELETE_WINDOW = reply->atom;
    free(reply);

    reply    = xcb_intern_atom_reply(connection, cookie_state, NULL);
    WM_STATE = reply->atom;
    free(reply);

    reply         = xcb_intern_atom_reply(connection, cookie_focus, NULL);
    WM_TAKE_FOCUS = reply->atom;
    free(reply);

    setupEWMH();
    setupEventHandlers();

    if (!loadConfig()) {
        fprintf(stderr, "banana: failed to load configuration\n");
        exit(1);
    }

    xcb_cursor_context_t* ctx;
    if (xcb_cursor_context_new(connection, screen, &ctx) >= 0) {
        normalCursor   = xcb_cursor_load_cursor(ctx, "left_ptr");
        moveCursor     = xcb_cursor_load_cursor(ctx, "fleur");
        resizeSECursor = xcb_cursor_load_cursor(ctx, "se-resize");
        resizeSWCursor = xcb_cursor_load_cursor(ctx, "sw-resize");
        resizeNECursor = xcb_cursor_load_cursor(ctx, "ne-resize");
        resizeNWCursor = xcb_cursor_load_cursor(ctx, "nw-resize");

        xcb_cursor_context_free(ctx);
    }

    uint32_t cursor_values[] = {normalCursor};
    xcb_change_window_attributes(connection, root, XCB_CW_CURSOR, cursor_values);

    uint32_t event_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                          XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
                          XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;

    uint32_t values[] = {event_mask};
    xcb_change_window_attributes(connection, root, XCB_CW_EVENT_MASK, values);

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

    xcb_flush(connection);
}

void checkCursorPosition(struct timeval* lastCheck, int* lastCursorX, int* lastCursorY, xcb_window_t* lastWindow) {
    struct timeval now;
    gettimeofday(&now, NULL);

    int elapsed_ms = ((now.tv_sec - lastCheck->tv_sec) * 1000) + ((now.tv_usec - lastCheck->tv_usec) / 1000);

    if (elapsed_ms <= 50)
        return;

    memcpy(lastCheck, &now, sizeof(struct timeval));

    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(connection, root);
    xcb_query_pointer_reply_t* reply  = xcb_query_pointer_reply(connection, cookie, NULL);

    if (!reply)
        return;

    int          root_x = reply->root_x;
    int          root_y = reply->root_y;
    xcb_window_t child  = reply->child;

    if (root_x == *lastCursorX && root_y == *lastCursorY && child == *lastWindow) {
        free(reply);
        return;
    }

    *lastCursorX = root_x;
    *lastCursorY = root_y;
    *lastWindow  = child;

    SMonitor* currentMonitor = monitorAtPoint(root_x, root_y);

    int       activeMonitor = -1;
    if (focused)
        activeMonitor = focused->monitor;

    SClient* windowUnderCursor = NULL;
    if (child != XCB_NONE && child != root)
        windowUnderCursor = findClient(child);

    free(reply);

    if (windowUnderCursor) {
        if (windowUnderCursor == focused)
            return;

        SMonitor* monitor = &monitors[windowUnderCursor->monitor];
        if (windowUnderCursor->workspace != monitor->currentWorkspace)
            return;

        fprintf(stderr, "Cursor over window 0x%x (currently focused: 0x%x)\n", windowUnderCursor->window, focused ? focused->window : 0);
        focusClient(windowUnderCursor);
        return;
    }

    if (activeMonitor != -1 && currentMonitor->num != activeMonitor) {
        SClient* clientInWorkspace = findVisibleClientInWorkspace(currentMonitor->num, currentMonitor->currentWorkspace);

        if (clientInWorkspace) {
            fprintf(stderr, "Focusing client on monitor %d\n", currentMonitor->num);
            focusClient(clientInWorkspace);
        } else {
            fprintf(stderr, "Focusing root on monitor %d\n", currentMonitor->num);
            focused = NULL;
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
            updateBorders();
            updateBars();
        }
    }
}

void run() {
    struct timeval lastCheck;
    int            lastCursorX = 0, lastCursorY = 0;
    xcb_window_t   lastWindow = XCB_NONE;
    gettimeofday(&lastCheck, NULL);

    xcb_flush(connection);
    fprintf(stderr, "Starting main event loop\n");

    updateClientVisibility();
    updateBars();

    while (1) {
        xcb_generic_event_t* event = xcb_poll_for_event(connection);

        if (event) {
            uint8_t event_type = event->response_type & ~0x80;

            if (eventHandlers[event_type])
                eventHandlers[event_type](event);

            free(event);
            xcb_flush(connection);
        } else {
            if (xcb_connection_has_error(connection)) {
                fprintf(stderr, "Connection to X server lost\n");
                break;
            }

            checkCursorPosition(&lastCheck, &lastCursorX, &lastCursorY, &lastWindow);
            usleep(5000);
        }
    }
}

void cleanup() {
    if (barWindows) {
        for (int i = 0; i < numMonitors; i++) {
            if (barWindows[i] != XCB_NONE)
                xcb_destroy_window(connection, barWindows[i]);
        }
        free(barWindows);
    }

    if (monitors)
        free(monitors);

    while (clients) {
        SClient* tmp = clients;
        clients      = clients->next;
        free(tmp);
    }

    if (wmcheckwin)
        xcb_destroy_window(connection, wmcheckwin);

    xcb_free_cursor(connection, normalCursor);
    xcb_free_cursor(connection, moveCursor);
    xcb_free_cursor(connection, resizeSECursor);
    xcb_free_cursor(connection, resizeSWCursor);
    xcb_free_cursor(connection, resizeNECursor);
    xcb_free_cursor(connection, resizeNWCursor);

    freeConfig();

    xcb_disconnect(connection);
}

void grabKeys() {
    xcb_ungrab_key(connection, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);

    xcb_key_symbols_t* keysyms = xcb_key_symbols_alloc(connection);

    for (size_t i = 0; i < keysCount; i++) {
        xcb_keycode_t* keycode = xcb_key_symbols_get_keycode(keysyms, keys[i].keysym);

        if (keycode) {
            xcb_grab_key(connection, 1, root, keys[i].mod, keycode[0], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(connection, 1, root, keys[i].mod | XCB_MOD_MASK_LOCK, keycode[0], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

            free(keycode);
        }
    }

    xcb_key_symbols_free(keysyms);
    xcb_flush(connection);

    fprintf(stderr, "Key grabs set up on root window\n");
}

void handleKeyPress(xcb_generic_event_t* event) {
    xcb_key_press_event_t* ev = (xcb_key_press_event_t*)event;

    xcb_key_symbols_t*     keysyms = xcb_key_symbols_alloc(connection);
    xcb_keysym_t           keysym  = xcb_key_symbols_get_keysym(keysyms, ev->detail, 0);
    xcb_key_symbols_free(keysyms);

    uint16_t state = ev->state & ~XCB_MOD_MASK_LOCK;

    lastMappedWindow = 0;

    for (size_t i = 0; i < keysCount; i++) {
        if (keys[i].keysym == keysym && keys[i].mod == state) {
            keys[i].func(keys[i].arg);
            break;
        }
    }
}

void handleButtonPress(xcb_generic_event_t* event) {
    xcb_button_press_event_t* ev            = (xcb_button_press_event_t*)event;
    xcb_window_t              clickedWindow = ev->event;

    lastMappedWindow = 0;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows && barWindows[i] == clickedWindow) {
            handleBarClick(event);
            return;
        }
    }

    SClient* client = findClient(clickedWindow);

    if (!client || (ev->state & modkey) == 0)
        return;

    if (ev->detail == XCB_BUTTON_INDEX_1) {
        if (!client->isFloating) {
            client->isFloating      = 1;
            windowMovement.wasTiled = 1;

            SMonitor* monitor = &monitors[client->monitor];

            int       oldWidth  = client->width;
            int       oldHeight = client->height;

            int       newWidth  = oldWidth * 0.8;
            int       newHeight = oldHeight * 0.8;

            newWidth  = MAX(20, newWidth);
            newHeight = MAX(10, newHeight);

            if (client->sizeHints.valid) {
                if (client->sizeHints.minWidth > 0 && newWidth < client->sizeHints.minWidth)
                    newWidth = client->sizeHints.minWidth;
                if (client->sizeHints.minHeight > 0 && newHeight < client->sizeHints.minHeight)
                    newHeight = client->sizeHints.minHeight;
            }

            int newX = ev->root_x - newWidth / 2;
            int newY = ev->root_y - newHeight / 2;

            client->x      = newX;
            client->y      = newY;
            client->width  = newWidth;
            client->height = newHeight;

            uint32_t values[4];
            values[0] = client->x;
            values[1] = client->y;
            values[2] = client->width;
            values[3] = client->height;
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

            configureClient(client);

            uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);

            windowMovement.client = client;
            windowMovement.x      = ev->root_x;
            windowMovement.y      = ev->root_y;
            windowMovement.active = 1;

            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, moveCursor, XCB_CURRENT_TIME);

            arrangeClients(monitor);
        } else {
            windowMovement.client   = client;
            windowMovement.x        = ev->root_x;
            windowMovement.y        = ev->root_y;
            windowMovement.active   = 1;
            windowMovement.wasTiled = 0;

            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, moveCursor, XCB_CURRENT_TIME);
        }
    } else if (ev->detail == XCB_BUTTON_INDEX_3 && client->isFloating) {
        windowResize.client = client;
        windowResize.x      = ev->root_x;
        windowResize.y      = ev->root_y;
        windowResize.active = 1;

        int relX = ev->root_x - client->x;
        int relY = ev->root_y - client->y;

        int cornerWidth  = client->width * 0.5;
        int cornerHeight = client->height * 0.5;

        if (relX < cornerWidth && relY < cornerHeight) {
            windowResize.resizeType = 3;
            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeNWCursor, XCB_CURRENT_TIME);
        } else if (relX > client->width - cornerWidth && relY < cornerHeight) {
            windowResize.resizeType = 2;
            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeNECursor, XCB_CURRENT_TIME);
        } else if (relX < cornerWidth && relY > client->height - cornerHeight) {
            windowResize.resizeType = 1;
            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeSWCursor, XCB_CURRENT_TIME);
        } else if (relX > client->width - cornerWidth && relY > client->height - cornerHeight) {
            windowResize.resizeType = 0;
            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeSECursor, XCB_CURRENT_TIME);
        } else {
            windowResize.resizeType = 0;
            xcb_grab_pointer(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeSECursor, XCB_CURRENT_TIME);
        }
    }
}

void swapWindowUnderCursor(SClient* client, int cursorX, int cursorY) {
    if (!client || !client->isFloating || !windowMovement.wasTiled)
        return;

    SClient* targetClient = NULL;

    for (SClient* c = clients; c; c = c->next) {
        if (c != client && c->monitor == client->monitor && c->workspace == client->workspace && !c->isFloating && !c->isFullscreen && cursorX >= c->x &&
            cursorX < c->x + c->width && cursorY >= c->y && cursorY < c->y + c->height) {
            targetClient = c;
            break;
        }
    }

    if (!targetClient) {
        int closestDistance = INT_MAX;

        for (SClient* c = clients; c; c = c->next) {
            if (c != client && c->monitor == client->monitor && c->workspace == client->workspace && !c->isFloating && !c->isFullscreen) {

                int centerX  = c->x + c->width / 2;
                int centerY  = c->y + c->height / 2;
                int distance = (cursorX - centerX) * (cursorX - centerX) + (cursorY - centerY) * (cursorY - centerY);

                if (distance < closestDistance) {
                    closestDistance = distance;
                    targetClient    = c;
                }
            }
        }
    }

    if (targetClient) {
        fprintf(stderr, "Swapping client 0x%x with 0x%x\n", client->window, targetClient->window);
        client->isFloating = 0;

        swapClients(client, targetClient);

        arrangeClients(&monitors[client->monitor]);
    } else {
        client->isFloating = 0;

        arrangeClients(&monitors[client->monitor]);
    }
}

void handleButtonRelease(xcb_generic_event_t* event) {
    xcb_button_release_event_t* ev = (xcb_button_release_event_t*)event;

    if (windowMovement.active) {
        SClient* movingClient = windowMovement.client;

        if (movingClient && windowMovement.wasTiled) {
            fprintf(stderr, "Attempting to swap with window under cursor at %d,%d\n", ev->root_x, ev->root_y);
            swapWindowUnderCursor(movingClient, ev->root_x, ev->root_y);
        } else if (movingClient)
            moveWindow(movingClient, movingClient->x, movingClient->y);

        windowMovement.active   = 0;
        windowMovement.client   = NULL;
        windowMovement.wasTiled = 0;
        xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
        xcb_flush(connection);

        updateBars();
    }

    if (windowResize.active) {
        windowResize.active = 0;
        windowResize.client = NULL;
        xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
        xcb_flush(connection);

        updateBars();
    }
}

SClient* clientAtPoint(int x, int y) {
    SClient* client = clients;

    while (client) {
        if (x >= client->x && x < client->x + client->width && y >= client->y && y < client->y + client->height)
            return client;
        client = client->next;
    }

    return NULL;
}

void handleMotionNotify(xcb_generic_event_t* event) {
    xcb_motion_notify_event_t* ev             = (xcb_motion_notify_event_t*)event;
    static int                 lastMonitor    = -1;
    SMonitor*                  currentMonitor = monitorAtPoint(ev->root_x, ev->root_y);

    if (!windowMovement.active && !windowResize.active) {
        if (lastMonitor != currentMonitor->num) {
            lastMonitor = currentMonitor->num;
            updateBars();

            currentWorkspace = currentMonitor->currentWorkspace;

            if (!focused || focused->monitor != currentMonitor->num) {
                SClient* clientInWorkspace = findVisibleClientInWorkspace(currentMonitor->num, currentMonitor->currentWorkspace);

                if (clientInWorkspace)
                    focusClient(clientInWorkspace);
                else {
                    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
                    focused = NULL;
                    updateBorders();
                }
            }
        }

        xcb_generic_event_t* motion_event;
        while ((motion_event = xcb_poll_for_event(connection)) != NULL) {
            if ((motion_event->response_type & ~0x80) == XCB_MOTION_NOTIFY) {
                if (((xcb_motion_notify_event_t*)motion_event)->event == ev->event) {
                    free(motion_event);
                    continue;
                }
            }
            xcb_allow_events(connection, XCB_ALLOW_SYNC_POINTER, XCB_CURRENT_TIME);
            break;
        }
    }

    if (windowMovement.active && windowMovement.client) {
        int dx = ev->root_x - windowMovement.x;
        int dy = ev->root_y - windowMovement.y;

        moveWindow(windowMovement.client, windowMovement.client->x + dx, windowMovement.client->y + dy);

        windowMovement.x = ev->root_x;
        windowMovement.y = ev->root_y;
    } else if (windowResize.active && windowResize.client) {
        int      dx     = ev->root_x - windowResize.x;
        int      dy     = ev->root_y - windowResize.y;
        SClient* client = windowResize.client;

        if (client->isFullscreen) {
            windowResize.x = ev->root_x;
            windowResize.y = ev->root_y;
            return;
        }

        if (!client->isFloating) {
            windowResize.x = ev->root_x;
            windowResize.y = ev->root_y;
            return;
        }

        int isFixedSize = (client->sizeHints.valid && client->sizeHints.maxWidth && client->sizeHints.maxHeight && client->sizeHints.minWidth && client->sizeHints.minHeight &&
                           client->sizeHints.maxWidth == client->sizeHints.minWidth && client->sizeHints.maxHeight == client->sizeHints.minHeight);

        xcb_atom_t windowType = getAtomProperty(client, NET_WM_WINDOW_TYPE);
        if (isFixedSize || windowType == NET_WM_WINDOW_TYPE_UTILITY) {
            windowResize.x = ev->root_x;
            windowResize.y = ev->root_y;
            return;
        }

        int newWidth, newHeight, newX, newY;

        switch (windowResize.resizeType) {
            case 1:
                newWidth  = client->width - dx;
                newHeight = client->height + dy;
                newX      = client->x + dx;
                newY      = client->y;

                if (newWidth >= 15) {
                    client->x     = newX;
                    client->width = newWidth;
                }
                if (newHeight >= 15)
                    client->height = newHeight;
                break;

            case 2:
                newWidth  = client->width + dx;
                newHeight = client->height - dy;
                newX      = client->x;
                newY      = client->y + dy;

                if (newWidth >= 15)
                    client->width = newWidth;
                if (newHeight >= 15) {
                    client->y      = newY;
                    client->height = newHeight;
                }
                break;

            case 3:
                newWidth  = client->width - dx;
                newHeight = client->height - dy;
                newX      = client->x + dx;
                newY      = client->y + dy;

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
            default: resizeWindow(windowResize.client, windowResize.client->width + dx, windowResize.client->height + dy); break;
        }

        if (windowResize.resizeType != 0) {
            if (client->sizeHints.valid) {
                if (client->sizeHints.minWidth > 0 && client->width < client->sizeHints.minWidth) {
                    int oldWidth  = client->width;
                    client->width = client->sizeHints.minWidth;
                    if (windowResize.resizeType == 1 || windowResize.resizeType == 3)
                        client->x -= (client->width - oldWidth);
                }

                if (client->sizeHints.minHeight > 0 && client->height < client->sizeHints.minHeight) {
                    int oldHeight  = client->height;
                    client->height = client->sizeHints.minHeight;
                    if (windowResize.resizeType == 2 || windowResize.resizeType == 3)
                        client->y -= (client->height - oldHeight);
                }

                if (client->sizeHints.maxWidth > 0 && client->width > client->sizeHints.maxWidth)
                    client->width = client->sizeHints.maxWidth;

                if (client->sizeHints.maxHeight > 0 && client->height > client->sizeHints.maxHeight)
                    client->height = client->sizeHints.maxHeight;
            }

            uint32_t values[4] = {client->x, client->y, client->width, client->height};
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

            uint32_t stack_values[] = {XCB_STACK_MODE_ABOVE};
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, stack_values);

            xcb_flush(connection);
            configureClient(client);
        }

        windowResize.x = ev->root_x;
        windowResize.y = ev->root_y;
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

    if (prevMonitor != client->monitor)
        client->workspace = monitor->currentWorkspace;

    uint32_t values[2] = {client->x, client->y};
    xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    xcb_flush(connection);

    configureClient(client);

    if (prevMonitor != client->monitor) {
        fprintf(stderr, "Window moved to a different monitor, updating layout\n");

        if (windowMovement.active && windowMovement.client == client) {
            fprintf(stderr, "Window being dragged to different monitor, focusing it\n");
            focusClient(client);
        }

        arrangeClients(&monitors[prevMonitor]);
        arrangeClients(monitor);

        updateBars();
    }

    if (windowMovement.active && windowMovement.client == client) {
        uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);
    }
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

        uint32_t values[4] = {client->x, client->y, client->width, client->height};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        xcb_flush(connection);
        configureClient(client);
        return;
    }

    int oldWidth  = client->width;
    int oldHeight = client->height;

    client->width  = MAX(15, width);
    client->height = MAX(15, height);

    if (client->sizeHints.valid) {
        if (client->sizeHints.minWidth > 0 && client->width < client->sizeHints.minWidth)
            client->width = client->sizeHints.minWidth;
        if (client->sizeHints.minHeight > 0 && client->height < client->sizeHints.minHeight)
            client->height = client->sizeHints.minHeight;

        if (client->sizeHints.maxWidth > 0 && client->width > client->sizeHints.maxWidth)
            client->width = client->sizeHints.maxWidth;
        if (client->sizeHints.maxHeight > 0 && client->height > client->sizeHints.maxHeight)
            client->height = client->sizeHints.maxHeight;
    }

    if (oldWidth != client->width || oldHeight != client->height) {
        uint32_t values[2] = {client->width, client->height};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

        if (windowResize.active && windowResize.client == client) {
            uint32_t stack_values[] = {XCB_STACK_MODE_ABOVE};
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, stack_values);
        }

        xcb_flush(connection);
        configureClient(client);
    }
}

void handleEnterNotify(xcb_generic_event_t* event) {
    xcb_enter_notify_event_t* ev = (xcb_enter_notify_event_t*)event;

    if (windowMovement.active || windowResize.active)
        return;

    if (ignoreNextEnterNotify) {
        ignoreNextEnterNotify = 0;
        return;
    }

    if (ev->mode != XCB_NOTIFY_MODE_NORMAL)
        return;

    if (ev->detail == XCB_NOTIFY_DETAIL_INFERIOR || ev->detail == XCB_NOTIFY_DETAIL_VIRTUAL || ev->detail == XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL)
        return;

    SClient* client = findClient(ev->event);
    if (!client || client == focused)
        return;

    fprintf(stderr, "Enter notify for window 0x%x\n", client->window);

    SMonitor* monitor = &monitors[client->monitor];
    if (client->workspace != monitor->currentWorkspace)
        return;

    focusClient(client);
}

void handleMapRequest(xcb_generic_event_t* event) {
    xcb_map_request_event_t* ev = (xcb_map_request_event_t*)event;

    manageClient(ev->window);
    xcb_map_window(connection, ev->window);

    lastMappedWindow = ev->window;
}

void handleConfigureRequest(xcb_generic_event_t* event) {
    xcb_configure_request_event_t* ev = (xcb_configure_request_event_t*)event;

    SClient*                       client = findClient(ev->window);
    if (client) {
        fprintf(stderr, "Configure request for managed window 0x%x\n", ev->window);

        if (client->isFullscreen) {
            SMonitor* monitor = &monitors[client->monitor];

            uint32_t  values[5];
            values[0] = monitor->x;
            values[1] = monitor->y;
            values[2] = monitor->width;
            values[3] = monitor->height;
            values[4] = 0;

            xcb_configure_window(connection, client->window,
                                 XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

            xcb_flush(connection);

            xcb_configure_notify_event_t ce;
            ce.response_type     = XCB_CONFIGURE_NOTIFY;
            ce.event             = client->window;
            ce.window            = client->window;
            ce.x                 = monitor->x;
            ce.y                 = monitor->y;
            ce.width             = monitor->width;
            ce.height            = monitor->height;
            ce.border_width      = 0;
            ce.above_sibling     = XCB_NONE;
            ce.override_redirect = 0;

            xcb_send_event(connection, 0, client->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&ce);
            return;
        }

        if (client->isFloating && !client->isFullscreen) {
            int updateX = (ev->value_mask & XCB_CONFIG_WINDOW_X);
            int updateY = (ev->value_mask & XCB_CONFIG_WINDOW_Y);
            int updateW = (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH);
            int updateH = (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT);

            if (updateX)
                client->x = ev->x;
            if (updateY)
                client->y = ev->y;
            if (updateW)
                client->width = ev->width;
            if (updateH)
                client->height = ev->height;

            uint32_t config_values[4];
            uint16_t config_mask = 0;
            int      value_index = 0;

            if (updateX || updateY || updateW || updateH) {
                if (updateX) {
                    config_values[value_index++] = client->x;
                    config_mask |= XCB_CONFIG_WINDOW_X;
                }
                if (updateY) {
                    config_values[value_index++] = client->y;
                    config_mask |= XCB_CONFIG_WINDOW_Y;
                }
                if (updateW) {
                    config_values[value_index++] = client->width;
                    config_mask |= XCB_CONFIG_WINDOW_WIDTH;
                }
                if (updateH) {
                    config_values[value_index++] = client->height;
                    config_mask |= XCB_CONFIG_WINDOW_HEIGHT;
                }

                xcb_configure_window(connection, client->window, config_mask, config_values);
                xcb_flush(connection);
            }
        }

        xcb_configure_notify_event_t ce;
        ce.response_type     = XCB_CONFIGURE_NOTIFY;
        ce.event             = client->window;
        ce.window            = client->window;
        ce.x                 = client->x;
        ce.y                 = client->y;
        ce.width             = client->width;
        ce.height            = client->height;
        ce.border_width      = client->isFullscreen ? 0 : borderWidth;
        ce.above_sibling     = XCB_NONE;
        ce.override_redirect = 0;

        xcb_send_event(connection, 0, client->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&ce);
    } else {
        uint32_t config_values[7];
        uint16_t config_mask = 0;
        int      value_index = 0;

        if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
            config_values[value_index++] = ev->x;
            config_mask |= XCB_CONFIG_WINDOW_X;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
            config_values[value_index++] = ev->y;
            config_mask |= XCB_CONFIG_WINDOW_Y;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            config_values[value_index++] = ev->width;
            config_mask |= XCB_CONFIG_WINDOW_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            config_values[value_index++] = ev->height;
            config_mask |= XCB_CONFIG_WINDOW_HEIGHT;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
            config_values[value_index++] = ev->border_width;
            config_mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
            config_values[value_index++] = ev->sibling;
            config_mask |= XCB_CONFIG_WINDOW_SIBLING;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
            config_values[value_index++] = ev->stack_mode;
            config_mask |= XCB_CONFIG_WINDOW_STACK_MODE;
        }

        xcb_configure_window(connection, ev->window, config_mask, config_values);
        xcb_flush(connection);
    }
}

void handleUnmapNotify(xcb_generic_event_t* event) {
    xcb_unmap_notify_event_t* ev = (xcb_unmap_notify_event_t*)event;

    if (ev->event != root) {
        SClient* client = findClient(ev->window);
        if (client) {
            if (workspaceSwitchActive || (ev->response_type & 0x80))
                return;

            xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(connection, ev->window);
            xcb_get_window_attributes_reply_t* reply  = xcb_get_window_attributes_reply(connection, cookie, NULL);

            if (!reply) {
                fprintf(stderr, "Window %x confirmed destroyed, unmanaging\n", ev->window);
                unmanageClient(ev->window);
            } else {
                fprintf(stderr, "Window %x unmapped but still exists, preserving\n", ev->window);
                free(reply);
            }
        }
    }
}

void handleDestroyNotify(xcb_generic_event_t* event) {
    xcb_destroy_notify_event_t* ev     = (xcb_destroy_notify_event_t*)event;
    SClient*                    client = findClient(ev->window);

    if (client) {
        if (workspaceSwitchActive) {
            fprintf(stderr, "Ignoring destroy notify for window 0x%x during workspace switch\n", ev->window);
            return;
        }

        fprintf(stderr, "Destroy notify for window 0x%x\n", ev->window);
        unmanageClient(ev->window);
    }
}

void spawnProgram(const char* program) {
    if (!program)
        return;

    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "banana: fork failed for program '%s'\n", program);
        return;
    }

    if (pid == 0) {
        if (connection)
            close(xcb_get_file_descriptor(connection));

        setsid();

        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2)
                close(devnull);
        }

        execl("/bin/sh", "sh", "-c", program, NULL);
        fprintf(stderr, "banana: execl failed for program '%s': %s\n", program, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void killClient(const char* arg) {
    (void)arg;

    if (!focused)
        return;

    fprintf(stderr, "Killing client 0x%x\n", focused->window);

    if (!sendEvent(focused, WM_DELETE_WINDOW)) {
        xcb_grab_server(connection);

        xcb_kill_client(connection, focused->window);

        xcb_flush(connection);
        xcb_ungrab_server(connection);
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

void updateFocus() {
    if (!focused) {
        xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
        xcb_delete_property(connection, root, NET_ACTIVE_WINDOW);
        xcb_flush(connection);
        return;
    }

    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, focused->window, XCB_CURRENT_TIME);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &focused->window);
    xcb_flush(connection);
    updateBorders();
}

void focusClient(SClient* client) {
    if (!client)
        return;

    fprintf(stderr, "Attempting to focus: 0x%x\n", client->window);

    xcb_get_window_attributes_reply_t* wa = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, client->window), NULL);
    if (!wa) {
        fprintf(stderr, "  Window no longer exists\n");
        return;
    }

    if (wa->map_state != XCB_MAP_STATE_VIEWABLE) {
        fprintf(stderr, "  Window not viewable (state: %d)\n", wa->map_state);
        free(wa);
        return;
    }

    if (wa->override_redirect) {
        fprintf(stderr, "  Window has override_redirect set\n");
        free(wa);
        return;
    }

    focused = client;

    if ((windowMovement.active && windowMovement.client == client) || (windowResize.active && windowResize.client == client)) {
        uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);
    }

    if (!client->neverfocus) {
        xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, client->window, XCB_CURRENT_TIME);

        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &client->window);
    }

    sendEvent(client, WM_TAKE_FOCUS);
    client->isUrgent = 0;

    updateBorders();
    restackFloatingWindows();
    updateBars();

    free(wa);
}

void manageClient(xcb_window_t window) {
    if (findClient(window))
        return;

    xcb_get_window_attributes_reply_t* wa = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, window), NULL);
    if (!wa) {
        fprintf(stderr, "Cannot manage window 0x%x: failed to get attributes\n", window);
        return;
    }

    if (wa->override_redirect) {
        fprintf(stderr, "Skipping override_redirect window 0x%x\n", window);
        free(wa);
        return;
    }

    SClient* client = malloc(sizeof(SClient));
    if (!client) {
        fprintf(stderr, "Failed to allocate memory for client\n");
        free(wa);
        return;
    }

    int                        monitorNum = 0;
    int                        rootX, rootY;
    _Bool                      pointerQuerySuccess = 0;

    xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(connection, xcb_query_pointer(connection, root), NULL);
    if (pointer) {
        rootX               = pointer->root_x;
        rootY               = pointer->root_y;
        pointerQuerySuccess = 1;
        free(pointer);
    }

    if (pointerQuerySuccess) {
        monitorNum = monitorAtPoint(rootX, rootY)->num;
        fprintf(stderr, "Using monitor %d at cursor position for new window\n", monitorNum);
    } else if (focused) {
        monitorNum = focused->monitor;
        fprintf(stderr, "Falling back to focused monitor %d for new window\n", monitorNum);
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
    client->width           = 0;
    client->height          = 0;
    client->x               = 0;
    client->y               = 0;
    client->sizeHints.valid = 0;

    xcb_window_t              transientFor = XCB_NONE;
    xcb_get_property_cookie_t cookie       = xcb_get_property(connection, 0, window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t* reply        = xcb_get_property_reply(connection, cookie, NULL);
    if (reply && reply->type == XCB_ATOM_WINDOW && reply->format == 32 && xcb_get_property_value_length(reply) >= 4) {
        transientFor    = *((xcb_window_t*)xcb_get_property_value(reply));
        SClient* parent = findClient(transientFor);
        if (parent) {
            client->monitor    = parent->monitor;
            client->workspace  = parent->workspace;
            client->isFloating = 1;

            client->x = parent->x + 50;
            client->y = parent->y + 50;

            fprintf(stderr, "Transient window detected, attached to parent 0x%x\n", transientFor);
        }
        free(reply);
    }

    SMonitor* monitor = &monitors[client->monitor];

    updateSizeHints(client);

    applyRules(client);

    if (client->sizeHints.valid && client->sizeHints.maxWidth && client->sizeHints.maxHeight && client->sizeHints.minWidth && client->sizeHints.minHeight &&
        client->sizeHints.maxWidth == client->sizeHints.minWidth && client->sizeHints.maxHeight == client->sizeHints.minHeight) {
        client->isFloating = 1;
        fprintf(stderr, "Auto-floating fixed size window: %dx%d\n", client->sizeHints.minWidth, client->sizeHints.minHeight);
    }

    monitor = &monitors[client->monitor];

    if (client->width == 0 || client->height == 0) {
        xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), NULL);
        if (geom) {
            if (geom->width > monitor->width - 2 * borderWidth)
                client->width = monitor->width - 2 * borderWidth;
            else
                client->width = geom->width;

            if (geom->height > monitor->height - 2 * borderWidth)
                client->height = monitor->height - 2 * borderWidth;
            else
                client->height = geom->height;

            free(geom);
        }

        if (client->sizeHints.valid && client->isFloating) {
            if (client->sizeHints.minWidth > 0 && client->width < client->sizeHints.minWidth)
                client->width = client->sizeHints.minWidth;
            if (client->sizeHints.minHeight > 0 && client->height < client->sizeHints.minHeight)
                client->height = client->sizeHints.minHeight;
        }

        client->x = monitor->x + (monitor->width - client->width) / 2;
        client->y = monitor->y + (monitor->height - client->height) / 2;
    }

    if (client->y < monitor->y + barHeight && !client->isFloating)
        client->y = monitor->y + barHeight;

    if (client->isFloating) {
        if (client->x == 0 && client->y == 0) {
            client->x = monitor->x + (monitor->width - client->width) / 2;
            client->y = monitor->y + (monitor->height - client->height) / 2;
        }

        if (client->x + client->width < monitor->x)
            client->x = monitor->x;
        if (client->y + client->height < monitor->y)
            client->y = monitor->y;
        if (client->x > monitor->x + monitor->width)
            client->x = monitor->x + monitor->width - client->width;
        if (client->y > monitor->y + monitor->height)
            client->y = monitor->y + monitor->height - client->height;

        if (client->y < monitor->y + barHeight)
            client->y = monitor->y + barHeight;
    }

    client->next = NULL;
    if (!clients)
        clients = client;
    else {
        SClient* last = clients;
        while (last->next)
            last = last->next;
        last->next = client;
    }

    uint32_t values[4];
    values[0] = client->x;
    values[1] = client->y;
    values[2] = client->width;
    values[3] = client->height;

    xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

    uint32_t border_width = borderWidth;
    xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &border_width);

    uint32_t event_mask =
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_MOTION;

    xcb_change_window_attributes(connection, window, XCB_CW_EVENT_MASK, &event_mask);

    updateWindowType(client);
    updateWMHints(client);

    if (!client->isFullscreen) {
        uint32_t values[3] = {modkey, XCB_BUTTON_INDEX_1, XCB_GRAB_MODE_ASYNC};
        xcb_grab_button(connection, 0, window, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                        XCB_NONE, moveCursor, values[1], values[0]);
    }

    if (client->isFloating) {
        uint32_t values[3] = {modkey, XCB_BUTTON_INDEX_3, XCB_GRAB_MODE_ASYNC};
        xcb_grab_button(connection, 0, window, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                        XCB_NONE, resizeSECursor, values[1], values[0]);
    }

    xcb_flush(connection);

    fprintf(stderr, "Client managed: 0x%x on monitor %d at position %d,%d with size %dx%d\n", window, client->monitor, client->x, client->y, client->width, client->height);

    updateBorders();

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, 1, (unsigned char*)&window);

    long data[2];
    data[0] = XCB_ICCCM_WM_STATE_NORMAL;
    data[1] = XCB_NONE;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, client->window, WM_STATE, WM_STATE, 32, 2, data);

    configureClient(client);

    if (wa->map_state == XCB_MAP_STATE_VIEWABLE) {
        fprintf(stderr, "Window is viewable, focusing now\n");
        focusClient(client);
    } else
        fprintf(stderr, "Window not yet viewable (state: %d), deferring focus\n", wa->map_state);

    arrangeClients(monitor);

    if (!client->isFloating && !client->isFullscreen) {
        uint32_t values = XCB_STACK_MODE_BELOW;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &values);
    }

    restackFloatingWindows();

    updateBars();

    updateClientList();

    free(wa);
}

void unmanageClient(xcb_window_t window) {
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
            xcb_delete_property(connection, root, NET_ACTIVE_WINDOW);
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
    updateClientVisibility();
    updateBars();

    updateClientList();
}

void configureClient(SClient* client) {
    if (!client)
        return;

    uint32_t values[5];
    uint32_t mask = 0;

    values[0] = client->x;
    values[1] = client->y;
    values[2] = client->width;
    values[3] = client->height;
    values[4] = client->isFullscreen ? 0 : borderWidth;

    mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH;

    xcb_configure_window(connection, client->window, mask, values);

    xcb_configure_notify_event_t event = {0};

    event.response_type     = XCB_CONFIGURE_NOTIFY;
    event.event             = client->window;
    event.window            = client->window;
    event.x                 = client->x;
    event.y                 = client->y;
    event.width             = client->width;
    event.height            = client->height;
    event.border_width      = client->isFullscreen ? 0 : borderWidth;
    event.above_sibling     = XCB_NONE;
    event.override_redirect = 0;

    xcb_send_event(connection, 0, client->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&event);

    updateBorders();
    xcb_flush(connection);
}

int parseHexColor(const char* hex, uint16_t* r, uint16_t* g, uint16_t* b) {
    if (!hex || *hex == '\0')
        return 0;

    const char* colorCode = hex;
    if (colorCode[0] == '#')
        colorCode++;

    unsigned int rgb;
    if (sscanf(colorCode, "%x", &rgb) != 1)
        return 0;

    *r = ((rgb >> 16) & 0xFF) * 257;
    *g = ((rgb >> 8) & 0xFF) * 257;
    *b = (rgb & 0xFF) * 257;

    return 1;
}

void updateBorders() {
    static uint32_t activeBorder            = 0;
    static uint32_t inactiveBorder          = 0;
    static char*    lastActiveBorderColor   = NULL;
    static char*    lastInactiveBorderColor = NULL;

    if ((lastActiveBorderColor == NULL && activeBorderColor != NULL) ||
        (lastActiveBorderColor != NULL && activeBorderColor != NULL && strcmp(lastActiveBorderColor, activeBorderColor) != 0) ||
        (lastInactiveBorderColor == NULL && inactiveBorderColor != NULL) ||
        (lastInactiveBorderColor != NULL && inactiveBorderColor != NULL && strcmp(lastInactiveBorderColor, inactiveBorderColor) != 0)) {

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
        xcb_colormap_t colormap = screen->default_colormap;

        if (activeBorderColor) {
            uint16_t r, g, b;
            if (parseHexColor(activeBorderColor, &r, &g, &b)) {
                xcb_alloc_color_cookie_t cookie = xcb_alloc_color(connection, colormap, r, g, b);
                xcb_generic_error_t*     error  = NULL;
                xcb_alloc_color_reply_t* reply  = xcb_alloc_color_reply(connection, cookie, &error);

                if (reply) {
                    activeBorder = reply->pixel;
                    free(reply);
                } else {
                    fprintf(stderr, "banana: failed to allocate active border color '%s'", activeBorderColor);
                    if (error) {
                        fprintf(stderr, " (XCB error code: %d)\n", error->error_code);
                        free(error);
                    } else
                        fprintf(stderr, " (unknown error)\n");

                    activeBorder = screen->black_pixel;
                    fprintf(stderr, "banana: using black pixel for active border\n");
                }
            } else {
                fprintf(stderr, "banana: invalid hex color format '%s'\n", activeBorderColor);
                activeBorder = screen->black_pixel;
            }
        } else
            activeBorder = screen->black_pixel;

        if (inactiveBorderColor) {
            uint16_t r, g, b;
            if (parseHexColor(inactiveBorderColor, &r, &g, &b)) {
                xcb_alloc_color_cookie_t cookie = xcb_alloc_color(connection, colormap, r, g, b);
                xcb_generic_error_t*     error  = NULL;
                xcb_alloc_color_reply_t* reply  = xcb_alloc_color_reply(connection, cookie, &error);

                if (reply) {
                    inactiveBorder = reply->pixel;
                    free(reply);
                } else {
                    fprintf(stderr, "banana: failed to allocate inactive border color '%s'", inactiveBorderColor);
                    if (error) {
                        fprintf(stderr, " (XCB error code: %d)\n", error->error_code);
                        free(error);
                    } else
                        fprintf(stderr, " (unknown error)\n");

                    inactiveBorder = screen->black_pixel;
                    fprintf(stderr, "banana: using black pixel for inactive border\n");
                }
            } else {
                fprintf(stderr, "banana: invalid hex color format '%s'\n", inactiveBorderColor);
                inactiveBorder = screen->black_pixel;
            }
        } else
            inactiveBorder = screen->black_pixel;

        lastActiveBorderColor   = safeStrdup(activeBorderColor);
        lastInactiveBorderColor = safeStrdup(inactiveBorderColor);
    }

    SClient* client = clients;
    while (client) {
        uint32_t border_color = (client == focused) ? activeBorder : inactiveBorder;
        xcb_change_window_attributes(connection, client->window, XCB_CW_BORDER_PIXEL, &border_color);
        client = client->next;
    }
}

SClient* findClient(xcb_window_t window) {
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
    if (monitors) {
        for (int i = 0; i < numMonitors; i++) {
            free(monitors[i].masterFactors);
        }
        free(monitors);
    }

    monitors           = NULL;
    int oldNumMonitors = numMonitors;
    numMonitors        = 0;

    xcb_xinerama_is_active_cookie_t active_cookie = xcb_xinerama_is_active(connection);
    xcb_xinerama_is_active_reply_t* active_reply  = xcb_xinerama_is_active_reply(connection, active_cookie, NULL);

    int                             is_active = 0;
    if (active_reply) {
        is_active = active_reply->state;
        free(active_reply);
    }

    if (is_active) {
        xcb_xinerama_query_screens_cookie_t screens_cookie = xcb_xinerama_query_screens(connection);
        xcb_xinerama_query_screens_reply_t* screens_reply  = xcb_xinerama_query_screens_reply(connection, screens_cookie, NULL);

        if (screens_reply) {
            xcb_xinerama_screen_info_t* info = xcb_xinerama_query_screens_screen_info(screens_reply);
            numMonitors                      = xcb_xinerama_query_screens_screen_info_length(screens_reply);

            monitors = malloc(numMonitors * sizeof(SMonitor));
            for (int i = 0; i < numMonitors; i++) {
                monitors[i].x                = info[i].x_org;
                monitors[i].y                = info[i].y_org;
                monitors[i].width            = info[i].width;
                monitors[i].height           = info[i].height;
                monitors[i].num              = i;
                monitors[i].currentWorkspace = 0;
                monitors[i].currentLayout    = LAYOUT_TILED;
                monitors[i].masterCount      = 1;
                monitors[i].masterFactors    = malloc(workspaceCount * sizeof(float));
                for (int ws = 0; ws < workspaceCount; ws++) {
                    monitors[i].masterFactors[ws] = defaultMasterFactor;
                }
            }

            free(screens_reply);
        }
    }

    if (!monitors) {
        numMonitors                  = 1;
        monitors                     = malloc(sizeof(SMonitor));
        monitors[0].x                = 0;
        monitors[0].y                = 0;
        monitors[0].width            = screen->width_in_pixels;
        monitors[0].height           = screen->height_in_pixels;
        monitors[0].num              = 0;
        monitors[0].currentWorkspace = 0;
        monitors[0].currentLayout    = LAYOUT_TILED;
        monitors[0].masterCount      = 1;
        monitors[0].masterFactors    = malloc(workspaceCount * sizeof(float));
        for (int ws = 0; ws < workspaceCount; ws++) {
            monitors[0].masterFactors[ws] = defaultMasterFactor;
        }
    }

    if (numMonitors != oldNumMonitors) {
        SClient* client = clients;
        while (client) {
            SMonitor* mon = getCurrentMonitor();
            int       x   = client->x;
            int       y   = client->y;
            if (x < mon->x || x >= mon->x + mon->width || y < mon->y || y >= mon->y + mon->height) {
                client->monitor = mon->num;
                client->x       = mon->x + (mon->width - client->width) / 2;
                client->y       = mon->y + (mon->height - client->height) / 2;
                if (client->y < mon->y + barHeight)
                    client->y = mon->y + barHeight;
            }
            client = client->next;
        }
    }
}

void handlePropertyNotify(xcb_generic_event_t* event) {
    xcb_property_notify_event_t* ev = (xcb_property_notify_event_t*)event;

    if (ev->window == root && ev->atom == XCB_ATOM_WM_NAME)
        updateStatus();
    else if (ev->atom == NET_WM_NAME || ev->atom == XCB_ATOM_WM_NAME) {
        SClient* client = findClient(ev->window);
        if (client)
            updateBars();
    }

    SClient* client = findClient(ev->window);
    if (client) {
        if (ev->atom == XCB_ATOM_WM_NORMAL_HINTS || ev->atom == XCB_ATOM_WM_HINTS)
            updateWMHints(client);
        else if (ev->atom == NET_WM_WINDOW_TYPE)
            updateWindowType(client);
        else if (ev->atom == NET_WM_STATE) {
            xcb_atom_t state = getAtomProperty(client, NET_WM_STATE);
            if (state == NET_WM_STATE_FULLSCREEN)
                setFullscreen(client, 1);
            else if (client->isFullscreen)
                setFullscreen(client, 0);
        }
    }
}

void handleExpose(xcb_generic_event_t* event) {
    xcb_expose_event_t* ev = (xcb_expose_event_t*)event;

    for (int i = 0; i < numMonitors; i++) {
        if (barWindows && barWindows[i] == ev->window) {
            handleBarExpose(event);
            return;
        }
    }
}

SClient* focusWindowUnderCursor(SMonitor* monitor) {
    int                        x, y;

    xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(connection, xcb_query_pointer(connection, root), NULL);

    if (pointer) {
        x = pointer->root_x;
        y = pointer->root_y;
        free(pointer);

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
        xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
        updateBorders();
        updateBars();
    }
    return NULL;
}

void switchToWorkspace(const char* arg) {
    int workspace = atoi(arg);
    if (workspace < 0 || workspace >= workspaceCount)
        return;

    SMonitor* monitor = getCurrentMonitor();
    if (!monitor)
        return;

    int previousWorkspace = monitor->currentWorkspace;
    if (workspace == previousWorkspace)
        return;

    workspaceSwitchActive = 1;

    monitor->currentWorkspace = workspace;
    currentWorkspace          = workspace;

    fprintf(stderr, "Switching to workspace %d (monitor %d)\n", workspace, monitor->num);

    uint32_t desktop = currentWorkspace;
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, 32, 1, &desktop);

    updateClientVisibility();
    updateBars();

    SClient* visibleClient = findVisibleClientInWorkspace(monitor->num, workspace);
    if (visibleClient)
        focusClient(visibleClient);
    else
        focusClient(NULL);

    workspaceSwitchActive = 0;
}

void moveClientToWorkspace(const char* arg) {
    if (!arg || !focused)
        return;

    int workspace = atoi(arg);
    if (workspace < 0 || workspace >= workspaceCount)
        return;

    SMonitor* currentMon  = &monitors[focused->monitor];
    SClient*  movedClient = focused;

    movedClient->workspace = workspace;

    moveClientToEnd(movedClient);

    if (workspace != currentMon->currentWorkspace) {
        workspaceSwitchActive = 1;
        xcb_unmap_window(connection, movedClient->window);
        xcb_flush(connection);
        workspaceSwitchActive = 0;

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
    workspaceSwitchActive = 1;

    SClient* client = clients;

    int      hasFullscreen[MAX_MONITORS][100] = {0};
    for (SClient* c = clients; c; c = c->next) {
        if (c->isFullscreen && c->monitor < MAX_MONITORS && c->workspace < workspaceCount)
            hasFullscreen[c->monitor][c->workspace] = 1;
    }

    while (client) {
        SMonitor* m = &monitors[client->monitor];
        if (client->workspace == m->currentWorkspace) {
            if (client->isFullscreen || !hasFullscreen[client->monitor][client->workspace])
                xcb_map_window(connection, client->window);
            else
                xcb_unmap_window(connection, client->window);
        } else
            xcb_unmap_window(connection, client->window);
        client = client->next;
    }

    xcb_flush(connection);
    workspaceSwitchActive = 0;
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
    int                        x, y;

    xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(connection, xcb_query_pointer(connection, root), NULL);

    if (pointer) {
        x = pointer->root_x;
        y = pointer->root_y;
        free(pointer);
        return monitorAtPoint(x, y);
    }

    return &monitors[0];
}

void moveClientToEnd(SClient* client) {
    if (!client || !clients)
        return;

    if (!client->next)
        return;

    if (clients == client)
        clients = client->next;
    else {
        SClient* prev = clients;
        while (prev && prev->next != client)
            prev = prev->next;

        if (!prev)
            return;

        prev->next = client->next;
    }

    SClient* last = clients;
    while (last->next)
        last = last->next;

    last->next   = client;
    client->next = NULL;
}

void toggleFloating(const char* arg) {
    (void)arg;

    if (!focused)
        return;

    if (focused->isFullscreen)
        return;

    int wasFloating = focused->isFloating;

    int isFixedSize = (focused->sizeHints.valid && focused->sizeHints.maxWidth && focused->sizeHints.maxHeight && focused->sizeHints.minWidth && focused->sizeHints.minHeight &&
                       focused->sizeHints.maxWidth == focused->sizeHints.minWidth && focused->sizeHints.maxHeight == focused->sizeHints.minHeight);

    ignoreNextEnterNotify = 1;

    focused->isFloating = !focused->isFloating || isFixedSize;

    if (focused->isFloating) {
        SMonitor* monitor = &monitors[focused->monitor];

        if (monitor->currentLayout == LAYOUT_TILED) {
            int oldWidth  = focused->width;
            int oldHeight = focused->height;

            int newWidth  = oldWidth * 0.8;
            int newHeight = oldHeight * 0.8;

            newWidth  = MAX(20, newWidth);
            newHeight = MAX(10, newHeight);

            if (focused->sizeHints.valid) {
                if (focused->sizeHints.minWidth > 0 && newWidth < focused->sizeHints.minWidth)
                    newWidth = focused->sizeHints.minWidth;
                if (focused->sizeHints.minHeight > 0 && newHeight < focused->sizeHints.minHeight)
                    newHeight = focused->sizeHints.minHeight;
            }

            int xOffset = (oldWidth - newWidth) / 2;
            int yOffset = (oldHeight - newHeight) / 2;
            int newX    = focused->x + xOffset;
            int newY    = focused->y + yOffset;

            focused->x      = newX;
            focused->y      = newY;
            focused->width  = newWidth;
            focused->height = newHeight;

            uint32_t values[4];
            values[0] = focused->x;
            values[1] = focused->y;
            values[2] = focused->width;
            values[3] = focused->height;
            xcb_configure_window(connection, focused->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        }

        uint32_t eventMask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION;
        xcb_grab_button(connection, 0, focused->window, eventMask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, moveCursor, XCB_BUTTON_INDEX_1, modkey);

        xcb_grab_button(connection, 0, focused->window, eventMask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeSECursor, XCB_BUTTON_INDEX_3, modkey);

        uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(connection, focused->window, XCB_CONFIG_WINDOW_STACK_MODE, values);

        if (!focused->neverfocus)
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, focused->window, XCB_CURRENT_TIME);

        arrangeClients(&monitors[focused->monitor]);
    } else if (wasFloating) {
        SMonitor* newMonitor = monitorAtPoint(focused->x + focused->width / 2, focused->y + focused->height / 2);

        if (newMonitor->num != focused->monitor) {
            int oldMonitor     = focused->monitor;
            focused->monitor   = newMonitor->num;
            focused->workspace = newMonitor->currentWorkspace;

            arrangeClients(&monitors[oldMonitor]);
        }

        moveClientToEnd(focused);

        uint32_t values[1] = {XCB_STACK_MODE_BELOW};
        xcb_configure_window(connection, focused->window, XCB_CONFIG_WINDOW_STACK_MODE, values);

        arrangeClients(&monitors[focused->monitor]);
    }

    updateBorders();
    restackFloatingWindows();
    updateBars();
}

void arrangeClients(SMonitor* monitor) {
    if (!monitor)
        return;

    tileClients(monitor);
}

void restackFloatingWindows() {
    for (int m = 0; m < numMonitors; m++) {
        SMonitor* monitor = &monitors[m];

        for (SClient* c = clients; c; c = c->next) {
            if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && !c->isFloating) {
                uint32_t values[1] = {XCB_STACK_MODE_BELOW};
                xcb_configure_window(connection, c->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
            }
        }

        for (SClient* c = clients; c; c = c->next) {
            if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && c->isFloating) {
                if (c == focused && (windowMovement.active || windowResize.active)) {
                    uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
                    xcb_configure_window(connection, c->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
                }
            }
        }
    }
}

void tileClients(SMonitor* monitor) {
    if (!monitor)
        return;

    SClient* visibleClients[MAX_CLIENTS] = {NULL};
    int      visibleCount                = 0;
    int      currentWorkspace            = monitor->currentWorkspace;

    for (SClient* client = clients; client; client = client->next) {
        if (client->monitor == monitor->num && client->workspace == monitor->currentWorkspace && !client->isFloating && !client->isFullscreen) {
            visibleClients[visibleCount++] = client;
            if (visibleCount >= MAX_CLIENTS)
                break;
        }
    }

    if (visibleCount <= 1)
        monitor->masterFactors[currentWorkspace] = defaultMasterFactor;

    float masterFactor = monitor->masterFactors[currentWorkspace];

    int   x               = monitor->x + outerGap;
    int   barBottom       = barVisible ? (monitor->y + barStrutsTop + barHeight + barBorderWidth * 2) : monitor->y;
    int   y               = barBottom + outerGap;
    int   availableWidth  = monitor->width - (2 * outerGap);
    int   availableHeight = monitor->height - (barVisible ? (barStrutsTop + barHeight + barBorderWidth * 2) : 0) - (2 * outerGap);

    int   masterArea = availableWidth * masterFactor;
    int   stackArea  = availableWidth - masterArea;

    if (visibleCount == 0)
        return;

    if (visibleCount == 1) {
        SClient* client = visibleClients[0];
        int      width  = availableWidth - 2 * borderWidth;
        int      height = availableHeight - 2 * borderWidth;

        client->x      = x;
        client->y      = y;
        client->width  = width;
        client->height = height;

        uint32_t values[4];
        values[0] = client->x;
        values[1] = client->y;
        values[2] = client->width;
        values[3] = client->height;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        configureClient(client);
        fprintf(stderr, "Single window tiled: monitor=%d pos=%d,%d size=%dx%d\n", monitor->num, client->x, client->y, client->width, client->height);
        return;
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

    int masterY = y;
    int stackY  = y;

    int masterWidth = masterArea - innerGap / 2 - 2 * borderWidth;
    int stackWidth  = stackArea - innerGap / 2 - 2 * borderWidth;
    int stackX      = x + masterArea + innerGap / 2;

    for (int i = 0; i < masterCount; i++) {
        SClient* client           = visibleClients[i];
        int      heightAdjustment = (i < masterRemainder) ? 1 : 0;
        int      currentHeight    = masterHeight + heightAdjustment;

        if (i > 0) {
            masterY += innerGap;
            currentHeight -= innerGap;
        }

        int width  = masterWidth;
        int height = currentHeight - 2 * borderWidth;

        client->x      = x;
        client->y      = masterY;
        client->width  = width;
        client->height = height;

        uint32_t values[4];
        values[0] = client->x;
        values[1] = client->y;
        values[2] = client->width;
        values[3] = client->height;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        configureClient(client);

        masterY += currentHeight;
    }

    for (int i = 0; i < stackCount; i++) {
        SClient* client           = visibleClients[i + masterCount];
        int      heightAdjustment = (i < stackRemainder) ? 1 : 0;
        int      currentHeight    = stackHeight + heightAdjustment;

        if (i > 0) {
            stackY += innerGap;
            currentHeight -= innerGap;
        }

        int width  = stackWidth;
        int height = currentHeight - 2 * borderWidth;

        client->x      = stackX;
        client->y      = stackY;
        client->width  = width;
        client->height = height;

        uint32_t values[4];
        values[0] = client->x;
        values[1] = client->y;
        values[2] = client->width;
        values[3] = client->height;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        configureClient(client);

        stackY += currentHeight;
    }
}

void warpPointerToClientCenter(SClient* client) {
    int centerX = client->x + client->width / 2;
    int centerY = client->y + client->height / 2;

    xcb_warp_pointer(connection, XCB_NONE, root, 0, 0, 0, 0, centerX, centerY);
    xcb_flush(connection);
}

void moveWindowInStack(const char* arg) {
    if (!focused || !arg)
        return;

    if (focused->isFloating)
        return;

    SMonitor* monitor   = &monitors[focused->monitor];
    int       workspace = focused->workspace;

    SClient*  targetClient = NULL;

    SClient*  workspaceClients[MAX_CLIENTS];
    int       numClients   = 0;
    int       focusedIndex = -1;

    for (SClient* c = clients; c; c = c->next) {
        if (c->monitor == focused->monitor && c->workspace == workspace && !c->isFloating && !c->isFullscreen) {

            if (c == focused)
                focusedIndex = numClients;

            workspaceClients[numClients++] = c;
            if (numClients >= MAX_CLIENTS)
                break;
        }
    }

    if (numClients <= 1)
        return;

    if (strcmp(arg, "up") == 0) {
        int targetIndex = (focusedIndex - 1 + numClients) % numClients;
        targetClient    = workspaceClients[targetIndex];
    } else if (strcmp(arg, "down") == 0) {
        int targetIndex = (focusedIndex + 1) % numClients;
        targetClient    = workspaceClients[targetIndex];
    }

    if (targetClient && targetClient != focused) {
        fprintf(stderr, "Moving window in stack: 0x%x with 0x%x (direction: %s)\n", focused->window, targetClient->window, arg);
        swapClients(focused, targetClient);
        arrangeClients(monitor);
        restackFloatingWindows();

        ignoreNextEnterNotify = 1;
        warpPointerToClientCenter(focused);
        updateBorders();
    }
}

void focusWindowInStack(const char* arg) {
    if (!focused || !arg)
        return;

    SMonitor* monitor = getCurrentMonitor();
    if (focused->monitor != monitor->num || focused->workspace != monitor->currentWorkspace)
        return;

    int      workspace    = focused->workspace;
    SClient* targetClient = NULL;

    SClient* tiledClients[MAX_CLIENTS];
    SClient* floatingClients[MAX_CLIENTS];
    int      numTiled             = 0;
    int      numFloating          = 0;
    int      focusedTiledIndex    = -1;
    int      focusedFloatingIndex = -1;

    for (SClient* c = clients; c; c = c->next) {
        if (c->monitor == focused->monitor && c->workspace == workspace && !c->isFullscreen) {
            if (c->isFloating) {
                if (c == focused)
                    focusedFloatingIndex = numFloating;
                floatingClients[numFloating++] = c;
            } else {
                if (c == focused)
                    focusedTiledIndex = numTiled;
                tiledClients[numTiled++] = c;
            }
            if (numTiled + numFloating >= MAX_CLIENTS)
                break;
        }
    }

    if (numTiled + numFloating <= 1)
        return;

    if (focused->isFloating) {
        if (strcmp(arg, "up") == 0) {
            if (focusedFloatingIndex > 0)
                targetClient = floatingClients[focusedFloatingIndex - 1];
            else if (numTiled > 0)
                targetClient = tiledClients[numTiled - 1];
            else
                targetClient = floatingClients[numFloating - 1];
        } else if (strcmp(arg, "down") == 0) {
            if (focusedFloatingIndex < numFloating - 1)
                targetClient = floatingClients[focusedFloatingIndex + 1];
            else if (numTiled > 0)
                targetClient = tiledClients[0];
            else
                targetClient = floatingClients[0];
        }
    } else {
        if (strcmp(arg, "up") == 0) {
            if (focusedTiledIndex > 0)
                targetClient = tiledClients[focusedTiledIndex - 1];
            else if (numFloating > 0)
                targetClient = floatingClients[numFloating - 1];
            else
                targetClient = tiledClients[numTiled - 1];
        } else if (strcmp(arg, "down") == 0) {
            if (focusedTiledIndex < numTiled - 1)
                targetClient = tiledClients[focusedTiledIndex + 1];
            else if (numFloating > 0)
                targetClient = floatingClients[0];
            else
                targetClient = tiledClients[0];
        }
    }

    if (targetClient && targetClient != focused) {
        fprintf(stderr, "Focusing window in stack: 0x%x (direction: %s, floating: %d)\n", targetClient->window, arg, targetClient->isFloating);
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
        if (c->monitor == monitor->num && c->workspace == monitor->currentWorkspace && !c->isFloating) {
            uint32_t values = XCB_STACK_MODE_BELOW;
            xcb_configure_window(connection, c->window, XCB_CONFIG_WINDOW_STACK_MODE, &values);
        }
    }
}

void swapClients(SClient* a, SClient* b) {
    if (!a || !b || a == b)
        return;

    fprintf(stderr, "Swapping clients in list: 0x%x and 0x%x\n", a->window, b->window);

    SClient* aNext = a->next;
    SClient* bNext = b->next;

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

    if (a->next == b) {
        if (prevA)
            prevA->next = b;
        else
            clients = b;

        b->next = a;
        a->next = bNext;
    } else if (b->next == a) {
        if (prevB)
            prevB->next = a;
        else
            clients = a;

        a->next = b;
        b->next = aNext;
    } else {
        if (prevA)
            prevA->next = b;
        else
            clients = b;

        if (prevB)
            prevB->next = a;
        else
            clients = a;

        a->next = bNext;
        b->next = aNext;
    }

    if (a->next == a)
        a->next = b;
    if (b->next == b)
        b->next = a;
}

void updateClientList() {
    int      count  = 0;
    SClient* client = clients;
    while (client && count < MAX_CLIENTS) {
        count++;
        client = client->next;
    }

    if (count == 0) {
        xcb_delete_property(connection, root, NET_CLIENT_LIST);
        return;
    }

    xcb_window_t* windowList = malloc(count * sizeof(xcb_window_t));
    if (!windowList) {
        fprintf(stderr, "Failed to allocate memory for client list\n");
        return;
    }

    client = clients;
    count  = 0;
    while (client && count < MAX_CLIENTS) {
        windowList[count++] = client->window;
        client              = client->next;
    }

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root, NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, count, windowList);

    free(windowList);
}

xcb_atom_t getAtomProperty(SClient* client, xcb_atom_t prop) {
    xcb_atom_t                atom = XCB_ATOM_NONE;

    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, client->window, prop, XCB_ATOM_ATOM, 0, 1);

    xcb_get_property_reply_t* reply = xcb_get_property_reply(connection, cookie, NULL);

    if (reply && reply->type == XCB_ATOM_ATOM && reply->format == 32 && xcb_get_property_value_length(reply) >= 4) {

        atom = *((xcb_atom_t*)xcb_get_property_value(reply));
    }

    if (reply)
        free(reply);

    return atom;
}

void setClientState(SClient* client, long state) {
    uint32_t data[2] = {state, XCB_NONE};

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, client->window, WM_STATE, WM_STATE, 32, 2, data);
}

int sendEvent(SClient* client, xcb_atom_t proto) {
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t* reply;
    xcb_atom_t*               protocols;
    int                       exists = 0;
    int                       n;

    cookie = xcb_get_property(connection, 0, client->window, WM_PROTOCOLS, XCB_ATOM_ATOM, 0, 1024);
    reply  = xcb_get_property_reply(connection, cookie, NULL);
    if (reply && reply->format == 32 && reply->type == XCB_ATOM_ATOM) {
        protocols = (xcb_atom_t*)xcb_get_property_value(reply);
        n         = xcb_get_property_value_length(reply) / sizeof(xcb_atom_t);

        for (int i = 0; i < n; i++) {
            if (protocols[i] == proto) {
                exists = 1;
                break;
            }
        }
    }

    if (reply)
        free(reply);

    if (exists) {
        xcb_client_message_event_t ev;

        memset(&ev, 0, sizeof(ev));
        ev.response_type  = XCB_CLIENT_MESSAGE;
        ev.window         = client->window;
        ev.type           = WM_PROTOCOLS;
        ev.format         = 32;
        ev.data.data32[0] = proto;
        ev.data.data32[1] = XCB_CURRENT_TIME;

        xcb_send_event(connection, 0, client->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)&ev);
        xcb_flush(connection);
    }

    return exists;
}

void setFullscreen(SClient* client, int fullscreen) {
    if (!client)
        return;

    if (client->isFullscreen == fullscreen)
        return;

    if (fullscreen) {
        fprintf(stderr, "Setting fullscreen for window 0x%x\n", client->window);

        client->oldx      = client->x;
        client->oldy      = client->y;
        client->oldwidth  = client->width;
        client->oldheight = client->height;
        client->oldState  = client->isFloating;

        client->isFloating   = 1;
        client->isFullscreen = 1;

        SMonitor* monitor = &monitors[client->monitor];

        client->x      = monitor->x;
        client->y      = monitor->y;
        client->width  = monitor->width;
        client->height = monitor->height;

        uint32_t values[] = {0};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

        xcb_atom_t fs_atom = NET_WM_STATE_FULLSCREEN;
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, client->window, NET_WM_STATE, XCB_ATOM_ATOM, 32, 1, &fs_atom);
        xcb_flush(connection);

        uint32_t values2[] = {client->x, client->y, client->width, client->height};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values2);

        uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);

        configureClient(client);
    } else {
        fprintf(stderr, "Unsetting fullscreen for window 0x%x\n", client->window);

        client->isFullscreen = 0;
        client->isFloating   = client->oldState;
        client->x            = client->oldx;
        client->y            = client->oldy;
        client->width        = client->oldwidth;
        client->height       = client->oldheight;

        uint32_t eventMask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION;

        xcb_grab_button(connection, 0, client->window, eventMask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, moveCursor, XCB_BUTTON_INDEX_1, modkey);

        if (client->isFloating)
            xcb_grab_button(connection, 0, client->window, eventMask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, resizeSECursor, XCB_BUTTON_INDEX_3, modkey);

        uint32_t values[] = {borderWidth};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

        uint32_t values2[] = {client->x, client->y, client->width, client->height};
        xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values2);

        configureClient(client);

        xcb_delete_property(connection, client->window, NET_WM_STATE);
        xcb_flush(connection);
    }

    SMonitor* monitor = &monitors[client->monitor];
    arrangeClients(monitor);
    updateClientVisibility();
    updateBars();
}

void updateWindowType(SClient* client) {
    xcb_atom_t state = getAtomProperty(client, NET_WM_STATE);
    xcb_atom_t wtype = getAtomProperty(client, NET_WM_WINDOW_TYPE);

    fprintf(stderr, "Checking window type for 0x%x, state=%d, wtype=%d\n", client->window, (int)state, (int)wtype);

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

            uint32_t values[] = {0};
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

            xcb_atom_t fs_atom = NET_WM_STATE_FULLSCREEN;
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, client->window, NET_WM_STATE, XCB_ATOM_ATOM, 32, 1, &fs_atom);

            uint32_t values2[] = {client->x, client->y, client->width, client->height};
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values2);

            configureClient(client);

            uint32_t stack_mode = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(connection, client->window, XCB_CONFIG_WINDOW_STACK_MODE, &stack_mode);
            xcb_flush(connection);
        }
    }
    if (wtype == NET_WM_WINDOW_TYPE_DIALOG || wtype == NET_WM_WINDOW_TYPE_UTILITY) {
        fprintf(stderr, "Dialog or utility window detected, forcing floating mode\n");
        client->isFloating = 1;
    }
}

void updateWMHints(SClient* client) {
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, client->window, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 0, 9);
    xcb_get_property_reply_t* reply  = xcb_get_property_reply(connection, cookie, NULL);

    if (reply && reply->format == 32 && xcb_get_property_value_length(reply) >= 9 * 4) {
        uint32_t* hints = (uint32_t*)xcb_get_property_value(reply);
        uint32_t  flags = hints[0];

        if (client == focused && (flags & (1 << 8))) {
            hints[0] &= ~(1 << 8);
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, client->window, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32, 9, hints);
        } else
            client->isUrgent = (flags & (1 << 8)) ? 1 : 0;

        if (flags & (1 << 0))
            client->neverfocus = !hints[1];
        else
            client->neverfocus = 0;

        free(reply);
    }
}

void handleClientMessage(xcb_generic_event_t* event) {
    xcb_client_message_event_t* cme = (xcb_client_message_event_t*)event;

    SClient*                    client = findClient(cme->window);

    if (!client)
        return;

    if (cme->type == NET_WM_STATE) {
        if (cme->data.data32[1] == NET_WM_STATE_FULLSCREEN || cme->data.data32[2] == NET_WM_STATE_FULLSCREEN) {
            int action = cme->data.data32[0];
            int enable = 0;

            if (action == 1)
                enable = 1;
            else if (action == 0)
                enable = 0;
            else if (action == 2)
                enable = !client->isFullscreen;

            setFullscreen(client, enable);
        }
    } else if (cme->type == NET_ACTIVE_WINDOW) {
        if (client != focused) {
            if (client->workspace != monitors[client->monitor].currentWorkspace) {
                client->isUrgent = 1;
                updateBorders();
                updateBars();
            } else
                focusClient(client);
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
    client->sizeHints.valid = 0;

    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, client->window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 18);
    xcb_get_property_reply_t* reply  = xcb_get_property_reply(connection, cookie, NULL);

    if (!reply || reply->type != XCB_ATOM_WM_SIZE_HINTS || reply->format != 32 || xcb_get_property_value_length(reply) < 18 * 4) {
        if (reply)
            free(reply);
        return;
    }

    uint32_t* hints = xcb_get_property_value(reply);
    uint32_t  flags = hints[0];

    if (flags & 16) {
        client->sizeHints.minWidth  = hints[5];
        client->sizeHints.minHeight = hints[6];
    } else {
        client->sizeHints.minWidth  = 0;
        client->sizeHints.minHeight = 0;
    }

    if (flags & 32) {
        client->sizeHints.maxWidth  = hints[7];
        client->sizeHints.maxHeight = hints[8];
    } else {
        client->sizeHints.maxWidth  = 0;
        client->sizeHints.maxHeight = 0;
    }

    if (flags & 256) {
        client->sizeHints.baseWidth  = hints[9];
        client->sizeHints.baseHeight = hints[10];
    } else {
        client->sizeHints.baseWidth  = 0;
        client->sizeHints.baseHeight = 0;
    }

    client->sizeHints.valid = 1;

    fprintf(stderr, "Size hints for 0x%x: min=%dx%d, max=%dx%d, base=%dx%d\n", client->window, client->sizeHints.minWidth, client->sizeHints.minHeight, client->sizeHints.maxWidth,
            client->sizeHints.maxHeight, client->sizeHints.baseWidth, client->sizeHints.baseHeight);

    free(reply);
}

void getWindowClass(xcb_window_t window, char* className, char* instanceName, size_t bufSize) {
    className[0]    = '\0';
    instanceName[0] = '\0';

    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 128);
    xcb_get_property_reply_t* reply  = xcb_get_property_reply(connection, cookie, NULL);

    if (reply && reply->type == XCB_ATOM_STRING && reply->format == 8) {
        char* data = (char*)xcb_get_property_value(reply);
        int   len  = xcb_get_property_value_length(reply);

        if (len > 0) {
            char* instance = data;
            char* class    = NULL;

            for (int i = 0; i < len - 1; i++) {
                if (data[i] == '\0') {
                    class = data + i + 1;
                    break;
                }
            }

            if (instance) {
                strncpy(instanceName, instance, bufSize - 1);
                instanceName[bufSize - 1] = '\0';
            }

            if (class) {
                strncpy(className, class, bufSize - 1);
                className[bufSize - 1] = '\0';
            }
        }

        free(reply);
    }
}

int applyRules(SClient* client) {
    char className[256]    = {0};
    char instanceName[256] = {0};

    getWindowClass(client->window, className, instanceName, sizeof(className));

    char*                     windowTitle = NULL;
    xcb_get_property_cookie_t cookie      = xcb_get_property(connection, 0, client->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 128);
    xcb_get_property_reply_t* reply       = xcb_get_property_reply(connection, cookie, NULL);

    if (reply && reply->type == XCB_ATOM_STRING && reply->format == 8) {
        windowTitle = malloc(xcb_get_property_value_length(reply) + 1);
        if (windowTitle) {
            memcpy(windowTitle, xcb_get_property_value(reply), xcb_get_property_value_length(reply));
            windowTitle[xcb_get_property_value_length(reply)] = '\0';
        }
    }

    for (size_t i = 0; i < rulesCount; i++) {
        const SWindowRule* rule = &rules[i];

        if (rule->className && strcmp(rule->className, "*") != 0 && (!className[0] || strcasecmp(rule->className, className) != 0))
            continue;

        if (rule->instanceName && (!instanceName[0] || strcasecmp(rule->instanceName, instanceName) != 0))
            continue;

        if (rule->title && (!windowTitle || !strstr(windowTitle, rule->title)))
            continue;

        if (rule->isFloating != -1)
            client->isFloating = rule->isFloating;

        if (rule->workspace != -1)
            client->workspace = rule->workspace;

        if (rule->monitor != -1 && rule->monitor < numMonitors)
            client->monitor = rule->monitor;

        SMonitor* mon         = &monitors[client->monitor];
        int       sizeChanged = 0;

        if (rule->width > 0) {
            client->width = rule->width;
            sizeChanged   = 1;
        }

        if (rule->height > 0) {
            client->height = rule->height;
            sizeChanged    = 1;
        }

        if (sizeChanged && client->isFloating) {
            client->x = mon->x + (mon->width - client->width) / 2;
            client->y = mon->y + (mon->height - client->height) / 2;

            if (client->y < mon->y + barHeight)
                client->y = mon->y + barHeight;
        }

        fprintf(stderr, "Applied rule for window class=%s instance=%s title=%s\n", className, instanceName, windowTitle ? windowTitle : "(null)");

        if (reply)
            free(reply);
        if (windowTitle)
            free(windowTitle);

        return 1;
    }

    if (reply)
        free(reply);
    if (windowTitle)
        free(windowTitle);

    return 0;
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

    xcb_warp_pointer(connection, XCB_NONE, root, 0, 0, 0, 0, centerX, centerY);

    SClient* clientToFocus = findVisibleClientInWorkspace(targetMonitor, monitor->currentWorkspace);

    if (clientToFocus)
        focusClient(clientToFocus);
    else {
        if (focused) {
            if (focused->monitor != targetMonitor) {
                xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
                focused = NULL;
                updateBorders();
            }
        } else
            xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
    }
}

void toggleBar(const char* arg) {
    (void)arg;

    showHideBars(!barVisible);

    for (int i = 0; i < numMonitors; i++) {
        arrangeClients(&monitors[i]);
    }
}

void tileAllMonitors(void) {
    for (int i = 0; i < numMonitors; i++) {
        arrangeClients(&monitors[i]);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (strcmp(argv[1], "validate") == 0) {
            SConfigErrors errors;
            memset(&errors, 0, sizeof(SConfigErrors));

            int result = validateConfig(&errors);
            printConfigErrors(&errors);

            return result ? 0 : 1;
        } else {
            fprintf(stderr, "banana: unknown command '%s'\n", argv[1]);
            fprintf(stderr, "Usage: banana [validate]\n");
            return 1;
        }
    }

    signal(SIGCHLD, SIG_IGN);

    setup();
    scanExistingWindows();
    updateClientList();

    run();

    cleanup();

    return 0;
}
