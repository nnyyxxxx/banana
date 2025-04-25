#ifndef CONFIG_H
#define CONFIG_H

#include <X11/X.h>
#include <X11/keysym.h>
#include <stddef.h> /* For NULL */

/* Forward declarations */
void spawnProgram(const char* arg);
void killClient(const char* arg);
void quit(const char* arg);
void switchToWorkspace(const char* arg);
void moveClientToWorkspace(const char* arg);
void toggleFloating(const char* arg);

/* Number of workspaces */
#define WORKSPACE_COUNT 9

/* Layout configuration */
#define DEFAULT_MASTER_FACTOR 0.55
#define DEFAULT_MASTER_COUNT  1

/* Key definitions */
#define MODKEY Mod1Mask

/* Bar configuration */
#define BAR_HEIGHT  20
#define BAR_PADDING 4
#define BAR_FONT    "monospace-12"

/* Colors */
#define ACTIVE_BORDER_COLOR   "#CD78A0"
#define INACTIVE_BORDER_COLOR "#CD78A0"

/* Bar colors */
#define BAR_BACKGROUND_COLOR    "#000000"
#define BAR_FOREGROUND_COLOR    "#d3c1da"
#define BAR_BORDER_COLOR        "#000000"
#define BAR_ACTIVE_WS_COLOR     "#CD78A0"
#define BAR_ACTIVE_TEXT_COLOR   "#000000"
#define BAR_INACTIVE_TEXT_COLOR "#d3c1da"
#define BAR_STATUS_TEXT_COLOR   "#d3c1da"
#define BAR_TITLE_BG_COLOR      "#CD78A0"
#define BAR_TITLE_TEXT_COLOR    "#000000"

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
    /* Mod    Key    Function    Argument */
    {MODKEY, XK_q, spawnProgram, TERMINAL},
    {MODKEY, XK_c, killClient, NULL},
    {MODKEY, XK_w, quit, NULL},
    {MODKEY, XK_space, toggleFloating, NULL},

    /* Workspace switching */
    {MODKEY, XK_1, switchToWorkspace, "0"},
    {MODKEY, XK_2, switchToWorkspace, "1"},
    {MODKEY, XK_3, switchToWorkspace, "2"},
    {MODKEY, XK_4, switchToWorkspace, "3"},
    {MODKEY, XK_5, switchToWorkspace, "4"},
    {MODKEY, XK_6, switchToWorkspace, "5"},
    {MODKEY, XK_7, switchToWorkspace, "6"},
    {MODKEY, XK_8, switchToWorkspace, "7"},
    {MODKEY, XK_9, switchToWorkspace, "8"},

    /* Move client to workspace */
    {MODKEY | ShiftMask, XK_1, moveClientToWorkspace, "0"},
    {MODKEY | ShiftMask, XK_2, moveClientToWorkspace, "1"},
    {MODKEY | ShiftMask, XK_3, moveClientToWorkspace, "2"},
    {MODKEY | ShiftMask, XK_4, moveClientToWorkspace, "3"},
    {MODKEY | ShiftMask, XK_5, moveClientToWorkspace, "4"},
    {MODKEY | ShiftMask, XK_6, moveClientToWorkspace, "5"},
    {MODKEY | ShiftMask, XK_7, moveClientToWorkspace, "6"},
    {MODKEY | ShiftMask, XK_8, moveClientToWorkspace, "7"},
    {MODKEY | ShiftMask, XK_9, moveClientToWorkspace, "8"},
};

#endif /* CONFIG_H */
