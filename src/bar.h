#ifndef BAR_H
#define BAR_H

#include <X11/Xlib.h>

extern Window* barWindows;

void           createBars(void);
void           updateStatus(void);
void           updateBars(void);
void           raiseBars(void);
void           handleBarExpose(XEvent* event);
void           cleanupBars(void);
void           updateClientPositionsForBar(void);

#endif /* BAR_H */