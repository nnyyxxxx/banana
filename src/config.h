#ifndef CONFIG_H
#define CONFIG_H

#include <X11/X.h>
#include <X11/keysym.h>
#include <stddef.h> /* For NULL */

void spawnProgram(const char* arg);
void killClient(const char* arg);
void quit(const char* arg);
void switchToWorkspace(const char* arg);
void moveClientToWorkspace(const char* arg);
void toggleFloating(const char* arg);
void toggleFullscreen(const char* arg);
void moveWindowInStack(const char* arg);
void focusWindowInStack(const char* arg);
void adjustMasterFactor(const char* arg);
void focusMonitor(const char* arg);
void toggleBar(const char* arg);

typedef struct {
    unsigned int mod;
    KeySym       keysym;
    void (*func)(const char*);
    const char* arg;
} KeyBinding;

typedef struct {
    const char* className;
    const char* instanceName;
    const char* title;
    int         isFloating;
    int         workspace;
    int         monitor;
    int         width;
    int         height;
} WindowRule;

typedef struct {
    const char* name;
    void (*func)(const char*);
} FunctionMap;

typedef struct {
    const char*  name;
    unsigned int mask;
} ModifierMap;

extern int   workspace_count;
extern float default_master_factor;
extern int   default_master_count;
extern int   inner_gap;
extern int   outer_gap;
#define modkey Mod1Mask
extern int         bar_height;
extern char*       bar_font;
extern int         max_title_length;
extern int         show_bar;
extern int         bar_border_width;
extern int         bar_struts_top;
extern int         bar_struts_left;
extern int         bar_struts_right;
extern char*       active_border_color;
extern char*       inactive_border_color;
extern char*       bar_border_color;
extern char*       bar_background_color;
extern char*       bar_foreground_color;
extern char*       bar_active_ws_color;
extern char*       bar_urgent_ws_color;
extern char*       bar_active_text_color;
extern char*       bar_urgent_text_color;
extern char*       bar_inactive_text_color;
extern char*       bar_status_text_color;
extern int         border_width;
extern char*       terminal;
extern char*       launcher;
extern char*       wall;
extern char*       screenshot;

extern KeyBinding* keys;
extern size_t      keys_count;
extern WindowRule* rules;
extern size_t      rules_count;

int                load_config(void);
void               create_default_config(void);
void               free_config(void);

#endif /* CONFIG_H */
