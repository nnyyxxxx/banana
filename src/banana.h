#ifndef BANANA_H
#define BANANA_H

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xinerama.h>
#include <sys/time.h>

#include "config.h"
#include "bar.h"

#define MOUSEMASK (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION)

#define MAX_CLIENTS  64
#define MAX_MONITORS 16

typedef enum {
    LAYOUT_FLOATING,
    LAYOUT_TILED,
    LAYOUT_MAX
} ELayout;

typedef struct {
    int minWidth, minHeight;
    int maxWidth, maxHeight;
    int baseWidth, baseHeight;
    int incWidth, incHeight;
    int minAspectNum, minAspectDen;
    int maxAspectNum, maxAspectDen;
    int gravity;
    int valid;
} SSizeHints;

typedef struct SClient {
    xcb_window_t    window;
    int             x, y;
    int             width, height;
    int             oldx, oldy;
    int             oldwidth, oldheight;
    int             monitor;
    int             oldMonitor;
    int             workspace;
    int             isFloating;
    int             isFullscreen;
    int             neverfocus;
    int             isUrgent;
    int             oldState;
    SSizeHints      sizeHints;
    struct SClient* next;
} SClient;

typedef struct SMonitor {
    int     x, y;
    int     width, height;
    int     num;
    int     currentWorkspace;
    ELayout currentLayout;
    float*  masterFactors;
    int     masterCount;
} SMonitor;

typedef struct {
    int      x;
    int      y;
    SClient* client;
    int      active;
    int      wasTiled;
} SWindowMovement;

typedef struct {
    int      x;
    int      y;
    SClient* client;
    int      active;
    int      resizeType;
} SWindowResize;

typedef struct {
    int       x;
    int       active;
    SMonitor* monitor;
} SMFactAdjust;

typedef struct {
    const char* className;
    const char* instanceName;
    const char* title;
    int         isFloating;
    int         workspace;
    int         monitor;
    int         width;
    int         height;
} SRule;

void                     setup();
void                     run();
void                     cleanup();

void                     handleKeyPress(xcb_generic_event_t* event);
void                     handleButtonPress(xcb_generic_event_t* event);
void                     handleButtonRelease(xcb_generic_event_t* event);
void                     handleMotionNotify(xcb_generic_event_t* event);
void                     handleEnterNotify(xcb_generic_event_t* event);
void                     handleMapRequest(xcb_generic_event_t* event);
void                     handleConfigureRequest(xcb_generic_event_t* event);
void                     handleUnmapNotify(xcb_generic_event_t* event);
void                     handleDestroyNotify(xcb_generic_event_t* event);
void                     handleExpose(xcb_generic_event_t* event);
void                     handlePropertyNotify(xcb_generic_event_t* event);
void                     handleClientMessage(xcb_generic_event_t* event);

void                     spawnProgram(const char* program);
void                     killClient(const char* arg);
void                     quit(const char* arg);
void                     switchToWorkspace(const char* arg);
void                     moveClientToWorkspace(const char* arg);
void                     toggleFloating(const char* arg);
void                     toggleFullscreen(const char* arg);
void                     moveWindowInStack(const char* arg);
void                     focusWindowInStack(const char* arg);
void                     adjustMasterFactor(const char* arg);
void                     focusMonitor(const char* arg);
void                     moveClientToEnd(SClient* client);
void                     checkCursorPosition(struct timeval* lastCheck, int* lastCursorX, int* lastCursorY, xcb_window_t* lastWindow);
void                     swapWindowUnderCursor(SClient* client, int cursorX, int cursorY);

void                     grabKeys();
void                     updateFocus();
void                     focusClient(SClient* client);
void                     manageClient(xcb_window_t window);
void                     unmanageClient(xcb_window_t window);
void                     configureClient(SClient* client);
void                     updateBorders();
void                     moveWindow(SClient* client, int x, int y);
void                     resizeWindow(SClient* client, int width, int height);
void                     updateClientVisibility();
void                     updateClientList();
void                     restackFloatingWindows();
void                     warpPointerToClientCenter(SClient* client);
SClient*                 findVisibleClientInWorkspace(int monitor, int workspace);
SMonitor*                getCurrentMonitor();
xcb_atom_t               getAtomProperty(SClient* client, xcb_atom_t prop);
void                     setClientState(SClient* client, long state);
int                      sendEvent(SClient* client, xcb_atom_t proto);
void                     setFullscreen(SClient* client, int fullscreen);
void                     updateWindowType(SClient* client);
void                     updateWMHints(SClient* client);
void                     updateSizeHints(SClient* client);

void                     tileClients(SMonitor* monitor);
void                     arrangeClients(SMonitor* monitor);
void                     swapClients(SClient* a, SClient* b);

SClient*                 findClient(xcb_window_t window);
SClient*                 clientAtPoint(int x, int y);
SMonitor*                monitorAtPoint(int x, int y);
void                     updateMonitors();
void                     getWindowClass(xcb_window_t window, char* className, char* instanceName, size_t bufSize);
int                      applyRules(SClient* client);

extern xcb_connection_t* connection;
extern xcb_screen_t*     screen;
extern xcb_window_t      root;
extern SClient*          clients;
extern SMonitor*         monitors;
extern int               numMonitors;
extern SClient*          focused;
extern SWindowMovement   windowMovement;
extern SWindowResize     windowResize;
extern SMFactAdjust      mfactAdjust;

extern xcb_atom_t        WM_PROTOCOLS;
extern xcb_atom_t        WM_DELETE_WINDOW;
extern xcb_atom_t        WM_STATE;
extern xcb_atom_t        WM_TAKE_FOCUS;

extern xcb_atom_t        NET_SUPPORTED;
extern xcb_atom_t        NET_WM_NAME;
extern xcb_atom_t        NET_SUPPORTING_WM_CHECK;
extern xcb_atom_t        NET_CLIENT_LIST;
extern xcb_atom_t        NET_NUMBER_OF_DESKTOPS;
extern xcb_atom_t        NET_CURRENT_DESKTOP;
extern xcb_atom_t        NET_WM_STATE;
extern xcb_atom_t        NET_WM_STATE_FULLSCREEN;
extern xcb_atom_t        NET_WM_WINDOW_TYPE;
extern xcb_atom_t        NET_WM_WINDOW_TYPE_DIALOG;
extern xcb_atom_t        NET_WM_WINDOW_TYPE_UTILITY;
extern xcb_atom_t        NET_ACTIVE_WINDOW;
extern xcb_atom_t        UTF8_STRING;

SClient*                 focusWindowUnderCursor(SMonitor* monitor);

#endif // BANANA_H