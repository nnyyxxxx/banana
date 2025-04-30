#ifndef CONFIG_H
#define CONFIG_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stddef.h>

#define CONFIG_PATH      "/.config/banana/banana.conf"
#define MAX_LINE_LENGTH  1024
#define MAX_TOKEN_LENGTH 128
#define MAX_KEYS         100
#define MAX_RULES        50
#define MAX_ERRORS       100

#define SECTION_GENERAL    "general"
#define SECTION_BAR        "bar"
#define SECTION_DECORATION "decoration"
#define SECTION_BINDS      "binds"
#define SECTION_RULES      "rules"

typedef struct {
    unsigned int mod;
    KeySym       keysym;
    void (*func)(const char*);
    const char* arg;
} SKeyBinding;

typedef struct {
    const char* className;
    const char* instanceName;
    const char* title;
    int         isFloating;
    int         workspace;
    int         monitor;
    int         width;
    int         height;
} SWindowRule;

typedef struct {
    const char* name;
    void (*func)(const char*);
} SFunctionMap;

typedef struct {
    const char*  name;
    unsigned int mask;
} SModifierMap;

typedef struct {
    char message[MAX_LINE_LENGTH];
    char lineContent[MAX_LINE_LENGTH];
    int  lineNum;
    int  isFatal;
} SConfigError;

typedef struct {
    SConfigError errors[MAX_ERRORS];
    int          count;
} SConfigErrors;

typedef enum {
    TOKEN_HANDLER_LOAD,
    TOKEN_HANDLER_VALIDATE
} ETokenHandlerMode;

typedef struct {
    ETokenHandlerMode mode;
    SConfigErrors*    errors;
    int               hasErrors;
} STokenHandlerContext;

void         spawnProgram(const char* arg);
void         killClient(const char* arg);
void         quit(const char* arg);
void         switchToWorkspace(const char* arg);
void         moveClientToWorkspace(const char* arg);
void         toggleFloating(const char* arg);
void         toggleFullscreen(const char* arg);
void         moveWindowInStack(const char* arg);
void         focusWindowInStack(const char* arg);
void         adjustMasterFactor(const char* arg);
void         focusMonitor(const char* arg);
void         toggleBar(const char* arg);
void         reloadConfig(const char* arg);
char*        safeStrdup(const char* s);
int          loadConfig(void);
int          validateConfig(SConfigErrors* errors);
void         printConfigErrors(SConfigErrors* errors);

void         initDefaults(void);
void*        safeMalloc(size_t size);
char*        getConfigPath(void);
void         trim(char* str);
KeySym       getKeysym(const char* key);
unsigned int getModifier(const char* mod);
void (*getFunction(const char* name))(const char*);
void            freeTokens(char** tokens, int count);
void            addError(SConfigErrors* errors, const char* message, int lineNum, int isFatal);

extern Display* display;
extern Window   root;

extern int      workspaceCount;
extern float    defaultMasterFactor;
extern int      innerGap;
extern int      outerGap;
#define modkey Mod1Mask
extern int                barHeight;
extern char*              barFont;
extern int                showBar;
extern int                barBorderWidth;
extern int                barStrutsTop;
extern int                barStrutsLeft;
extern int                barStrutsRight;
extern char*              activeBorderColor;
extern char*              inactiveBorderColor;
extern char*              barBorderColor;
extern char*              barBackgroundColor;
extern char*              barForegroundColor;
extern char*              barActiveWsColor;
extern char*              barUrgentWsColor;
extern char*              barActiveTextColor;
extern char*              barUrgentTextColor;
extern char*              barInactiveTextColor;
extern char*              barStatusTextColor;
extern int                borderWidth;
extern char*              terminal;
extern char*              launcher;
extern char*              wall;
extern char*              screenshot;

extern int                showErrorNotifications;
extern char*              errorBorderColor;
extern int                errorBorderWidth;
extern char*              errorBackgroundColor;
extern char*              errorTextColor;
extern char*              errorFont;

extern SKeyBinding*       keys;
extern size_t             keysCount;
extern SWindowRule*       rules;
extern size_t             rulesCount;

extern const SFunctionMap functionMap[];
extern const SModifierMap modifierMap[];

int                       loadConfig(void);
void                      createDefaultConfig(void);
void                      freeConfig(void);

#endif /* CONFIG_H */
