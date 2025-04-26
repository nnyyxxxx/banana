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

#endif /* BAR_H */