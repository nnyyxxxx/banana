#ifndef CONFIG_H
#define CONFIG_H

#include <X11/X.h>
#include <X11/keysym.h>
#include <stddef.h> /* For NULL */

/* Forward declarations */
void spawnProgram(const char* arg);
void killClient(const char* arg);
void quit(const char* arg);

/* Key definitions */
#define MODKEY Mod1Mask

/* Colors */
#define ACTIVE_BORDER_COLOR   "#3584e4" /* Blue */
#define INACTIVE_BORDER_COLOR "#404040" /* Dark gray */

/* Border width in pixels */
#define BORDER_WIDTH 2

/* Applications */
#define TERMINAL "alacritty"

/* Key bindings */
static const struct {
    unsigned int mod;
    KeySym       keysym;
    void (*func)(const char*);
    const char* arg;
} keys[] = {
    /* Mod               Key        Function        Argument */
    {MODKEY, XK_q, spawnProgram, TERMINAL},
    {MODKEY, XK_c, killClient, NULL},
    {MODKEY, XK_w, quit, NULL},
};

#endif /* CONFIG_H */
