#ifndef BAR_H
#define BAR_H

#include <X11/Xlib.h>
#include "banana.h"

void           toggleBar(const char* arg);

extern Window* barWindows;
extern int     barVisible;

void           createBars(void);
void           updateStatus(void);
void           updateBars(void);
void           raiseBars(void);
void           handleBarExpose(XEvent* event);
void           handleBarClick(XEvent* event);
void           cleanupBars(void);
void           updateClientPositionsForBar(void);
void           showHideBars(int show);

#endif /* BAR_H */