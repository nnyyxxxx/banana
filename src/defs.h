#ifndef DEFS_H
#define DEFS_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>

#include "config.h"

typedef struct SClient {
    Window          window;
    int             x, y;
    int             width, height;
    int             monitor;
    struct SClient* next;
} SClient;

typedef struct SMonitor {
    int x, y;
    int width, height;
    int num;
} SMonitor;

typedef struct {
    int      x;
    int      y;
    SClient* client;
    int      active;
} SWindowMovement;

typedef struct {
    int      x;
    int      y;
    SClient* client;
    int      active;
} SWindowResize;

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

void                   spawnProgram(const char* program);
void                   killClient(const char* arg);
void                   quit(const char* arg);

void                   grabKeys();
void                   updateFocus();
void                   focusClient(SClient* client);
void                   manageClient(Window window);
void                   unmanageClient(Window window);
void                   configureClient(SClient* client);
void                   updateBorders();
void                   moveWindow(SClient* client, int x, int y);
void                   resizeWindow(SClient* client, int width, int height);

SClient*               findClient(Window window);
SMonitor*              monitorAtPoint(int x, int y);
void                   updateMonitors();

extern Display*        display;
extern Window          root;
extern SClient*        clients;
extern SMonitor*       monitors;
extern int             numMonitors;
extern SClient*        focused;
extern SWindowMovement windowMovement;
extern SWindowResize   windowResize;

#endif // DEFS_H
