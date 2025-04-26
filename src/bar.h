#ifndef BAR_H
#define BAR_H

#include <X11/Xlib.h>
#include "defs.h"

extern Window* barWindows;

void           createBars(void);
void           updateStatus(void);
void           updateBars(void);
void           raiseBars(void);
void           handleBarExpose(XEvent* event);
void           handleBarClick(XEvent* event);
void           cleanupBars(void);
void           updateClientPositionsForBar(void);
char*          getWindowTitle(SClient* client);

int            createSystray(void);
void           updateSystray(void);
void           removeSystrayIcon(Window win);
void           cleanupSystray(void);
void           handleSystrayClientMessage(XEvent* event);
int            getSystrayWidth(void);

#endif /* BAR_H */