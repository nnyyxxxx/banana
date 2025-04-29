#ifndef BANANA_H
#define BANANA_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>

#include "config.h"
#include "bar.h"

#define MOUSEMASK (ButtonPressMask | ButtonReleaseMask | ButtonMotionMask)
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

typedef enum {
    LAYOUT_FLOATING,
    LAYOUT_TILED,
    LAYOUT_MAX
} Layout;

typedef struct {
    int minWidth, minHeight;
    int maxWidth, maxHeight;
    int baseWidth, baseHeight;
    int incWidth, incHeight;
    int minAspectNum, minAspectDen;
    int maxAspectNum, maxAspectDen;
    int gravity;
    int valid;
} SizeHints;

typedef struct SClient {
    Window          window;
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
    SizeHints       sizeHints;
    struct SClient* next;
} SClient;

typedef struct SMonitor {
    int    x, y;
    int    width, height;
    int    num;
    int    currentWorkspace;
    Layout currentLayout;
    float  masterFactors[WORKSPACE_COUNT];
    int    masterCount;
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

void                   setup();
void                   run();
void                   cleanup();

void                   handleKeyPress(XEvent* event);
void                   handleButtonPress(XEvent* event);
void                   handleButtonRelease(XEvent* event);
void                   handleMotionNotify(XEvent* event);
void                   handleEnterNotify(XEvent* event);
void                   handleMapRequest(XEvent* event);
void                   handleConfigureRequest(XEvent* event);
void                   handleUnmapNotify(XEvent* event);
void                   handleDestroyNotify(XEvent* event);
void                   handleExpose(XEvent* event);
void                   handlePropertyNotify(XEvent* event);
void                   handleClientMessage(XEvent* event);

void                   spawnProgram(const char* program);
void                   killClient(const char* arg);
void                   quit(const char* arg);
void                   switchToWorkspace(const char* arg);
void                   moveClientToWorkspace(const char* arg);
void                   toggleFloating(const char* arg);
void                   toggleFullscreen(const char* arg);
void                   moveWindowInStack(const char* arg);
void                   focusWindowInStack(const char* arg);
void                   adjustMasterFactor(const char* arg);
void                   focusMonitor(const char* arg);
void                   moveClientToEnd(SClient* client);
void                   checkCursorPosition();
void                   swapWindowUnderCursor(SClient* client, int cursorX, int cursorY);

void                   grabKeys();
void                   updateFocus();
void                   focusClient(SClient* client);
void                   manageClient(Window window);
void                   unmanageClient(Window window);
void                   configureClient(SClient* client);
void                   updateBorders();
void                   moveWindow(SClient* client, int x, int y);
void                   resizeWindow(SClient* client, int width, int height);
void                   updateClientVisibility();
void                   updateClientList();
void                   restackFloatingWindows();
void                   warpPointerToClientCenter(SClient* client);
SClient*               findVisibleClientInWorkspace(int monitor, int workspace);
SMonitor*              getCurrentMonitor();
Atom                   getAtomProperty(SClient* client, Atom prop);
void                   setClientState(SClient* client, long state);
int                    sendEvent(SClient* client, Atom proto);
void                   setFullscreen(SClient* client, int fullscreen);
void                   updateWindowType(SClient* client);
void                   updateWMHints(SClient* client);
void                   updateSizeHints(SClient* client);

void                   tileClients(SMonitor* monitor);
void                   arrangeClients(SMonitor* monitor);
void                   swapClients(SClient* a, SClient* b);

SClient*               findClient(Window window);
SClient*               clientAtPoint(int x, int y);
SMonitor*              monitorAtPoint(int x, int y);
void                   updateMonitors();
void                   getWindowClass(Window window, char* className, char* instanceName, size_t bufSize);
int                    applyRules(SClient* client);

extern Display*        display;
extern Window          root;
extern SClient*        clients;
extern SMonitor*       monitors;
extern int             numMonitors;
extern SClient*        focused;
extern SWindowMovement windowMovement;
extern SWindowResize   windowResize;
extern SMFactAdjust    mfactAdjust;

extern Atom            WM_PROTOCOLS;
extern Atom            WM_DELETE_WINDOW;
extern Atom            WM_STATE;
extern Atom            WM_TAKE_FOCUS;

extern Atom            NET_SUPPORTED;
extern Atom            NET_WM_NAME;
extern Atom            NET_SUPPORTING_WM_CHECK;
extern Atom            NET_CLIENT_LIST;
extern Atom            NET_NUMBER_OF_DESKTOPS;
extern Atom            NET_CURRENT_DESKTOP;
extern Atom            NET_WM_STATE;
extern Atom            NET_WM_STATE_FULLSCREEN;
extern Atom            NET_WM_WINDOW_TYPE;
extern Atom            NET_WM_WINDOW_TYPE_DIALOG;
extern Atom            NET_WM_WINDOW_TYPE_UTILITY;
extern Atom            NET_ACTIVE_WINDOW;
extern Atom            UTF8_STRING;

SClient*               focusWindowUnderCursor(SMonitor* monitor);

#endif // BANANA_H