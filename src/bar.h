#ifndef BAR_H
#define BAR_H

#include <X11/Xlib.h>
#include "banana.h"

typedef struct SSystrayIcon {
    Window               win;
    struct SSystrayIcon* next;
} SSystrayIcon;

extern Atom          NET_SYSTEM_TRAY_OPCODE;
extern Atom          NET_SYSTEM_TRAY_ORIENTATION;
extern Atom          NET_SYSTEM_TRAY_VISUAL;
extern Atom          XEMBED;
extern Atom          XEMBED_INFO;
extern Window        systrayWin;
extern SSystrayIcon* systrayIcons;
extern int           systrayIconSize;
extern int           systraySpacing;

void                 toggleBar(const char* arg);

extern Window*       barWindows;
extern int           barVisible;

void                 createBars(void);
void                 updateStatus(void);
void                 updateBars(void);
void                 raiseBars(void);
void                 handleBarExpose(XEvent* event);
void                 handleBarClick(XEvent* event);
void                 cleanupBars(void);
void                 updateClientPositionsForBar(void);
void                 showHideBars(int show);

int                  createSystray(void);
void                 updateSystray(void);
void                 removeSystrayIcon(Window win);
void                 cleanupSystray(void);
void                 handleSystrayClientMessage(XEvent* event);
int                  getSystrayWidth(void);

#endif /* BAR_H */