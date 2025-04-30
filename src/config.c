#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "config.h"

extern Display* display;
extern Window   root;

#define CONFIG_PATH      "/.config/banana/banana.conf"
#define MAX_LINE_LENGTH  1024
#define MAX_TOKEN_LENGTH 128
#define MAX_KEYS         100
#define MAX_RULES        50

int                 workspaceCount       = 9;
float               defaultMasterFactor  = 0.55;
int                 defaultMasterCount   = 1;
int                 innerGap             = 15;
int                 outerGap             = 20;
int                 barHeight            = 20;
char*               barFont              = NULL;
int                 maxTitleLength       = 40;
int                 showBar              = 1;
int                 barBorderWidth       = 0;
int                 barStrutsTop         = 0;
int                 barStrutsLeft        = 0;
int                 barStrutsRight       = 0;
char*               activeBorderColor    = NULL;
char*               inactiveBorderColor  = NULL;
char*               barBorderColor       = NULL;
char*               barBackgroundColor   = NULL;
char*               barForegroundColor   = NULL;
char*               barActiveWsColor     = NULL;
char*               barUrgentWsColor     = NULL;
char*               barActiveTextColor   = NULL;
char*               barUrgentTextColor   = NULL;
char*               barInactiveTextColor = NULL;
char*               barStatusTextColor   = NULL;
int                 borderWidth          = 2;

SKeyBinding*        keys       = NULL;
size_t              keysCount  = 0;
SWindowRule*        rules      = NULL;
size_t              rulesCount = 0;

static SFunctionMap functionMap[] = {{"spawn", spawnProgram},
                                     {"kill", killClient},
                                     {"quit", quit},
                                     {"switch_workspace", switchToWorkspace},
                                     {"move_to_workspace", moveClientToWorkspace},
                                     {"toggle_floating", toggleFloating},
                                     {"toggle_fullscreen", toggleFullscreen},
                                     {"move_window", moveWindowInStack},
                                     {"focus_window", focusWindowInStack},
                                     {"adjust_master", adjustMasterFactor},
                                     {"focus_monitor", focusMonitor},
                                     {"toggle_bar", toggleBar},
                                     {"reload_config", reloadConfig},
                                     {NULL, NULL}};

static SModifierMap modifierMap[] = {{"alt", Mod1Mask}, {"shift", ShiftMask}, {"control", ControlMask}, {"super", Mod4Mask}, {NULL, 0}};

static void         initDefaults(void);
static void*        safeMalloc(size_t size);
static char*        safeStrdup(const char* s);
static char*        getConfigPath(void);
static void         trim(char* str);
static char**       tokenize(char* line, const char* delimiter, int* count);
static KeySym       getKeysym(const char* key);
static unsigned int getModifier(const char* mod);
static void (*getFunction(const char* name))(const char*);
static int  parseBindLine(char** tokens, int tokenCount);
static int  parseRuleLine(char** tokens, int tokenCount);
static int  parseSetLine(char** tokens, int tokenCount);
static void freeTokens(char** tokens, int count);

struct SMonitor;

static void initDefaults(void) {
    barFont              = safeStrdup("monospace-12");
    activeBorderColor    = safeStrdup("#6275d3");
    inactiveBorderColor  = safeStrdup("#6275d3");
    barBorderColor       = safeStrdup("#000000");
    barBackgroundColor   = safeStrdup("#000000");
    barForegroundColor   = safeStrdup("#ced4f0");
    barActiveWsColor     = safeStrdup("#6275d3");
    barUrgentWsColor     = safeStrdup("#6275d3");
    barActiveTextColor   = safeStrdup("#000000");
    barUrgentTextColor   = safeStrdup("#000000");
    barInactiveTextColor = safeStrdup("#ced4f0");
    barStatusTextColor   = safeStrdup("#ced4f0");
}

static void* safeMalloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "banana: failed to allocate memory\n");
        exit(1);
    }
    return ptr;
}

static char* safeStrdup(const char* s) {
    if (!s)
        return NULL;
    char* result = strdup(s);
    if (!result) {
        fprintf(stderr, "banana: failed to allocate memory for string\n");
        exit(1);
    }
    return result;
}

static char* getConfigPath(void) {
    const char* home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "banana: HOME environment variable not set\n");
        return NULL;
    }

    char* path = safeMalloc(strlen(home) + strlen(CONFIG_PATH) + 1);
    sprintf(path, "%s%s", home, CONFIG_PATH);
    return path;
}

static void trim(char* str) {
    if (!str)
        return;

    char* start = str;
    while (isspace((unsigned char)*start))
        start++;

    if (start != str)
        memmove(str, start, strlen(start) + 1);

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    *(end + 1) = '\0';
}

static char** tokenize(char* line, const char* delimiter, int* count) {
    if (!line || !delimiter || !count)
        return NULL;

    char** tokens = safeMalloc(MAX_TOKEN_LENGTH * sizeof(char*));
    *count        = 0;

    char* saveptr;
    char* token = strtok_r(line, delimiter, &saveptr);

    while (token && *count < MAX_TOKEN_LENGTH) {
        char* trimmed = safeStrdup(token);
        trim(trimmed);

        if (trimmed && *trimmed)
            tokens[(*count)++] = trimmed;
        else
            free(trimmed);

        token = strtok_r(NULL, delimiter, &saveptr);
    }

    return tokens;
}

static KeySym getKeysym(const char* key) {
    if (!key)
        return NoSymbol;

    if (strlen(key) == 1) {
        if (key[0] >= 'a' && key[0] <= 'z')
            return XK_a + (key[0] - 'a');

        return XStringToKeysym(key);
    }

    if (strcasecmp(key, "escape") == 0)
        return XK_Escape;
    if (strcasecmp(key, "return") == 0)
        return XK_Return;
    if (strcasecmp(key, "enter") == 0)
        return XK_Return;
    if (strcasecmp(key, "tab") == 0)
        return XK_Tab;
    if (strcasecmp(key, "space") == 0)
        return XK_space;
    if (strcasecmp(key, "backspace") == 0)
        return XK_BackSpace;

    if (strcasecmp(key, "f1") == 0)
        return XK_F1;
    if (strcasecmp(key, "f2") == 0)
        return XK_F2;
    if (strcasecmp(key, "f3") == 0)
        return XK_F3;
    if (strcasecmp(key, "f4") == 0)
        return XK_F4;
    if (strcasecmp(key, "f5") == 0)
        return XK_F5;
    if (strcasecmp(key, "f6") == 0)
        return XK_F6;
    if (strcasecmp(key, "f7") == 0)
        return XK_F7;
    if (strcasecmp(key, "f8") == 0)
        return XK_F8;
    if (strcasecmp(key, "f9") == 0)
        return XK_F9;
    if (strcasecmp(key, "f10") == 0)
        return XK_F10;
    if (strcasecmp(key, "f11") == 0)
        return XK_F11;
    if (strcasecmp(key, "f12") == 0)
        return XK_F12;

    if (strcasecmp(key, "up") == 0)
        return XK_Up;
    if (strcasecmp(key, "down") == 0)
        return XK_Down;
    if (strcasecmp(key, "left") == 0)
        return XK_Left;
    if (strcasecmp(key, "right") == 0)
        return XK_Right;

    return XStringToKeysym(key);
}

static unsigned int getModifier(const char* mod) {
    if (!mod)
        return 0;

    if (strchr(mod, '+')) {
        char*        modCopy = safeStrdup(mod);
        char*        token   = strtok(modCopy, "+");
        unsigned int result  = 0;

        while (token) {
            for (int i = 0; modifierMap[i].name; i++) {
                if (strcasecmp(token, modifierMap[i].name) == 0) {
                    result |= modifierMap[i].mask;
                    break;
                }
            }
            token = strtok(NULL, "+");
        }

        free(modCopy);
        return result;
    }

    for (int i = 0; modifierMap[i].name; i++) {
        if (strcasecmp(mod, modifierMap[i].name) == 0)
            return modifierMap[i].mask;
    }

    return 0;
}

static void (*getFunction(const char* name))(const char*) {
    if (!name)
        return NULL;

    for (int i = 0; functionMap[i].name; i++) {
        if (strcasecmp(name, functionMap[i].name) == 0)
            return functionMap[i].func;
    }

    return NULL;
}

static int parseBindLine(char** tokens, int tokenCount) {
    if (tokenCount < 4) {
        fprintf(stderr, "banana: invalid bind line, need at least 4 tokens\n");
        return 0;
    }

    const char* modStr  = tokens[1];
    const char* keyStr  = tokens[2];
    const char* funcStr = tokens[3];
    const char* argStr  = (tokenCount > 4) ? tokens[4] : NULL;

    KeySym      keysym = getKeysym(keyStr);
    if (keysym == NoSymbol) {
        fprintf(stderr, "banana: invalid key: %s\n", keyStr);
        return 0;
    }

    unsigned int mod = getModifier(modStr);
    if (!mod) {
        fprintf(stderr, "banana: unknown modifier: %s\n", modStr);
        return 0;
    }

    void (*func)(const char*) = getFunction(funcStr);
    if (!func) {
        fprintf(stderr, "banana: unknown function: %s\n", funcStr);
        return 0;
    }

    if (keysCount >= MAX_KEYS) {
        fprintf(stderr, "banana: too many key bindings (max: %d)\n", MAX_KEYS);
        return 0;
    }

    if (!keys)
        keys = safeMalloc(MAX_KEYS * sizeof(SKeyBinding));

    keys[keysCount].mod    = mod;
    keys[keysCount].keysym = keysym;
    keys[keysCount].func   = func;
    keys[keysCount].arg    = argStr ? safeStrdup(argStr) : NULL;

    keysCount++;
    return 1;
}

static int parseRuleLine(char** tokens, int tokenCount) {
    if (tokenCount < 3) {
        fprintf(stderr, "banana: invalid rule line, need at least 3 tokens\n");
        return 0;
    }

    if (rulesCount >= MAX_RULES) {
        fprintf(stderr, "banana: too many window rules (max: %d)\n", MAX_RULES);
        return 0;
    }

    if (!rules)
        rules = safeMalloc(MAX_RULES * sizeof(SWindowRule));

    rules[rulesCount].className    = safeStrdup(tokens[1]);
    rules[rulesCount].instanceName = (tokenCount > 2 && strcmp(tokens[2], "*") != 0) ? safeStrdup(tokens[2]) : NULL;
    rules[rulesCount].title        = (tokenCount > 3 && strcmp(tokens[3], "*") != 0) ? safeStrdup(tokens[3]) : NULL;
    rules[rulesCount].isFloating   = (tokenCount > 4) ? atoi(tokens[4]) : -1;
    rules[rulesCount].workspace    = (tokenCount > 5) ? atoi(tokens[5]) : -1;
    rules[rulesCount].monitor      = (tokenCount > 6) ? atoi(tokens[6]) : -1;
    rules[rulesCount].width        = (tokenCount > 7) ? atoi(tokens[7]) : -1;
    rules[rulesCount].height       = (tokenCount > 8) ? atoi(tokens[8]) : -1;

    rulesCount++;
    return 1;
}

static int parseSetLine(char** tokens, int tokenCount) {
    if (tokenCount < 3) {
        fprintf(stderr, "banana: invalid set line, need 3 tokens but got %d\n", tokenCount);
        return 0;
    }

    const char* var = tokens[1];
    const char* val = tokens[2];

    fprintf(stderr, "Setting %s to %s (tokenCount=%d)\n", var, val, tokenCount);

    if (strcmp(var, "workspace_count") == 0)
        workspaceCount = atoi(val);
    else if (strcmp(var, "default_master_count") == 0)
        defaultMasterCount = atoi(val);
    else if (strcmp(var, "inner_gap") == 0)
        innerGap = atoi(val);
    else if (strcmp(var, "outer_gap") == 0)
        outerGap = atoi(val);
    else if (strcmp(var, "bar_height") == 0)
        barHeight = atoi(val);
    else if (strcmp(var, "max_title_length") == 0)
        maxTitleLength = atoi(val);
    else if (strcmp(var, "show_bar") == 0)
        showBar = atoi(val);
    else if (strcmp(var, "bar_border_width") == 0)
        barBorderWidth = atoi(val);
    else if (strcmp(var, "bar_struts_top") == 0)
        barStrutsTop = atoi(val);
    else if (strcmp(var, "bar_struts_left") == 0)
        barStrutsLeft = atoi(val);
    else if (strcmp(var, "bar_struts_right") == 0)
        barStrutsRight = atoi(val);
    else if (strcmp(var, "border_width") == 0)
        borderWidth = atoi(val);
    else if (strcmp(var, "default_master_factor") == 0)
        defaultMasterFactor = atof(val);
    else if (strcmp(var, "bar_font") == 0) {
        free(barFont);
        barFont = safeStrdup(val);
    } else if (strcmp(var, "active_border_color") == 0) {
        free(activeBorderColor);
        activeBorderColor = safeStrdup(val);
    } else if (strcmp(var, "inactive_border_color") == 0) {
        free(inactiveBorderColor);
        inactiveBorderColor = safeStrdup(val);
    } else if (strcmp(var, "bar_border_color") == 0) {
        free(barBorderColor);
        barBorderColor = safeStrdup(val);
    } else if (strcmp(var, "bar_background_color") == 0) {
        free(barBackgroundColor);
        barBackgroundColor = safeStrdup(val);
    } else if (strcmp(var, "bar_foreground_color") == 0) {
        free(barForegroundColor);
        barForegroundColor = safeStrdup(val);
    } else if (strcmp(var, "bar_active_ws_color") == 0) {
        free(barActiveWsColor);
        barActiveWsColor = safeStrdup(val);
    } else if (strcmp(var, "bar_urgent_ws_color") == 0) {
        free(barUrgentWsColor);
        barUrgentWsColor = safeStrdup(val);
    } else if (strcmp(var, "bar_active_text_color") == 0) {
        free(barActiveTextColor);
        barActiveTextColor = safeStrdup(val);
    } else if (strcmp(var, "bar_urgent_text_color") == 0) {
        free(barUrgentTextColor);
        barUrgentTextColor = safeStrdup(val);
    } else if (strcmp(var, "bar_inactive_text_color") == 0) {
        free(barInactiveTextColor);
        barInactiveTextColor = safeStrdup(val);
    } else if (strcmp(var, "bar_status_text_color") == 0) {
        free(barStatusTextColor);
        barStatusTextColor = safeStrdup(val);
    } else {
        fprintf(stderr, "banana: unknown variable: %s\n", var);
        return 0;
    }

    return 1;
}

static void freeTokens(char** tokens, int count) {
    if (!tokens)
        return;

    for (int i = 0; i < count; i++) {
        free(tokens[i]);
    }

    free(tokens);
}

void createDefaultConfig(void) {
    char* configPath = getConfigPath();
    if (!configPath)
        return;

    char* dirPath   = safeStrdup(configPath);
    char* lastSlash = strrchr(dirPath, '/');
    if (lastSlash) {
        *lastSlash = '\0';

        struct stat st;
        if (stat(dirPath, &st) == -1) {
            if (mkdir(dirPath, 0755) == -1) {
                fprintf(stderr, "banana: failed to create config directory: %s\n", strerror(errno));
                free(dirPath);
                free(configPath);
                return;
            }
        }
    }
    free(dirPath);

    FILE* fp = fopen(configPath, "w");
    if (!fp) {
        fprintf(stderr, "banana: failed to create config file: %s\n", strerror(errno));
        free(configPath);
        return;
    }

    fprintf(fp, "# General settings\n");
    fprintf(fp, "set > workspace_count > 9\n");
    fprintf(fp, "set > default_master_factor > 0.55\n");
    fprintf(fp, "set > default_master_count > 1\n");
    fprintf(fp, "set > inner_gap > 15\n");
    fprintf(fp, "set > outer_gap > 20\n");
    fprintf(fp, "set > border_width > 2\n\n");

    fprintf(fp, "# Bar settings\n");
    fprintf(fp, "set > bar_height > 20\n");
    fprintf(fp, "set > bar_font > monospace-12\n");
    fprintf(fp, "set > max_title_length > 40\n");
    fprintf(fp, "set > show_bar > 1\n");
    fprintf(fp, "set > bar_border_width > 0\n");
    fprintf(fp, "set > bar_struts_top > 0\n");
    fprintf(fp, "set > bar_struts_left > 0\n");
    fprintf(fp, "set > bar_struts_right > 0\n\n");

    fprintf(fp, "# Colors\n");
    fprintf(fp, "set > active_border_color > #6275d3\n");
    fprintf(fp, "set > inactive_border_color > #6275d3\n");
    fprintf(fp, "set > bar_border_color > #000000\n");
    fprintf(fp, "set > bar_background_color > #000000\n");
    fprintf(fp, "set > bar_foreground_color > #ced4f0\n");
    fprintf(fp, "set > bar_active_ws_color > #6275d3\n");
    fprintf(fp, "set > bar_urgent_ws_color > #6275d3\n");
    fprintf(fp, "set > bar_active_text_color > #000000\n");
    fprintf(fp, "set > bar_urgent_text_color > #000000\n");
    fprintf(fp, "set > bar_inactive_text_color > #ced4f0\n");
    fprintf(fp, "set > bar_status_text_color > #ced4f0\n\n");

    fprintf(fp, "# Key bindings\n");
    fprintf(fp, "bind > alt > q > spawn > alacritty\n");
    fprintf(fp, "bind > alt > e > spawn > dmenu_run\n");
    fprintf(fp, "bind > alt > a > spawn > pocky\n");
    fprintf(fp, "bind > alt > escape > spawn > maim -s | xclip -selection clipboard -t image/png\n");
    fprintf(fp, "bind > alt > c > kill\n");
    fprintf(fp, "bind > alt > w > quit\n");
    fprintf(fp, "bind > alt > space > toggle_floating\n");
    fprintf(fp, "bind > alt > f > toggle_fullscreen\n");
    fprintf(fp, "bind > alt > b > toggle_bar\n");
    fprintf(fp, "bind > alt > r > reload_config\n\n");

    fprintf(fp, "bind > alt > h > adjust_master > decrease\n");
    fprintf(fp, "bind > alt > l > adjust_master > increase\n\n");

    fprintf(fp, "bind > alt+shift > j > move_window > down\n");
    fprintf(fp, "bind > alt+shift > k > move_window > up\n\n");

    fprintf(fp, "bind > alt > j > focus_window > down\n");
    fprintf(fp, "bind > alt > k > focus_window > up\n\n");

    fprintf(fp, "bind > alt > comma > focus_monitor > left\n");
    fprintf(fp, "bind > alt > period > focus_monitor > right\n\n");

    fprintf(fp, "bind > alt > 1 > switch_workspace > 0\n");
    fprintf(fp, "bind > alt > 2 > switch_workspace > 1\n");
    fprintf(fp, "bind > alt > 3 > switch_workspace > 2\n");
    fprintf(fp, "bind > alt > 4 > switch_workspace > 3\n");
    fprintf(fp, "bind > alt > 5 > switch_workspace > 4\n");
    fprintf(fp, "bind > alt > 6 > switch_workspace > 5\n");
    fprintf(fp, "bind > alt > 7 > switch_workspace > 6\n");
    fprintf(fp, "bind > alt > 8 > switch_workspace > 7\n");
    fprintf(fp, "bind > alt > 9 > switch_workspace > 8\n\n");

    fprintf(fp, "bind > alt+shift > 1 > move_to_workspace > 0\n");
    fprintf(fp, "bind > alt+shift > 2 > move_to_workspace > 1\n");
    fprintf(fp, "bind > alt+shift > 3 > move_to_workspace > 2\n");
    fprintf(fp, "bind > alt+shift > 4 > move_to_workspace > 3\n");
    fprintf(fp, "bind > alt+shift > 5 > move_to_workspace > 4\n");
    fprintf(fp, "bind > alt+shift > 6 > move_to_workspace > 5\n");
    fprintf(fp, "bind > alt+shift > 7 > move_to_workspace > 6\n");
    fprintf(fp, "bind > alt+shift > 8 > move_to_workspace > 7\n");
    fprintf(fp, "bind > alt+shift > 9 > move_to_workspace > 8\n\n");

    fprintf(fp, "# Window rules (class, instance, title, floating, workspace, monitor, width, height)\n");
    fprintf(fp, "rule > Pocky > * > * > 1 > -1 > -1 > 1100 > 700\n");
    fprintf(fp, "rule > vesktop > * > * > -1 > 0 > 1 > -1 > -1\n");

    fclose(fp);
    fprintf(stderr, "banana: created default config file at %s\n", configPath);
    free(configPath);
}

int loadConfig(void) {
    initDefaults();

    free(keys);
    keys      = NULL;
    keysCount = 0;

    for (size_t i = 0; i < rulesCount; i++) {
        free((char*)rules[i].className);
        free((char*)rules[i].instanceName);
        free((char*)rules[i].title);
    }
    free(rules);
    rules      = NULL;
    rulesCount = 0;

    char* configPath = getConfigPath();
    if (!configPath)
        return 0;

    FILE* fp = fopen(configPath, "r");
    if (!fp) {
        fprintf(stderr, "banana: config file not found at %s, creating default\n", configPath);
        free(configPath);
        createDefaultConfig();

        configPath = getConfigPath();
        if (!configPath)
            return 0;

        fp = fopen(configPath, "r");
        if (!fp) {
            fprintf(stderr, "banana: failed to open config file: %s\n", strerror(errno));
            free(configPath);
            return 0;
        }
    }

    free(configPath);

    char line[MAX_LINE_LENGTH];
    char originalLine[MAX_LINE_LENGTH];
    int  lineNum = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineNum++;

        strcpy(originalLine, line);

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char* comment     = NULL;
        int   inColorCode = 0;

        for (char* p = line; *p; p++) {
            if (*p == '#' && isxdigit((unsigned char)*(p + 1)))
                continue;
            if (*p == '#' && !inColorCode) {
                comment = p;
                break;
            }
        }

        if (comment)
            *comment = '\0';

        trim(line);
        if (line[0] == '\0')
            continue;

        fprintf(stderr, "Line %d: '%s' (original: '%s')\n", lineNum, line, originalLine);

        int    tokenCount = 0;
        char** tokens     = tokenize(line, ">", &tokenCount);

        fprintf(stderr, "  Tokenized into %d tokens: ", tokenCount);
        for (int i = 0; i < tokenCount; i++) {
            fprintf(stderr, "'%s'%s", tokens[i], (i < tokenCount - 1) ? ", " : "\n");
        }

        if (tokenCount < 2) {
            fprintf(stderr, "banana: line %d: invalid format\n", lineNum);
            freeTokens(tokens, tokenCount);
            continue;
        }

        if (strcasecmp(tokens[0], "bind") == 0)
            parseBindLine(tokens, tokenCount);
        else if (strcasecmp(tokens[0], "rule") == 0)
            parseRuleLine(tokens, tokenCount);
        else if (strcasecmp(tokens[0], "set") == 0)
            parseSetLine(tokens, tokenCount);
        else
            fprintf(stderr, "banana: line %d: unknown directive: %s\n", lineNum, tokens[0]);

        freeTokens(tokens, tokenCount);
    }

    fclose(fp);

    fprintf(stderr, "banana: loaded %zu key bindings and %zu window rules\n", keysCount, rulesCount);
    return 1;
}

void reloadConfig(const char* arg) {
    (void)arg;

    fprintf(stderr, "banana: reloading configuration...\n");

    SKeyBinding* oldKeys = keys;
    size_t oldKeysCount = keysCount;
    SWindowRule* oldRules = rules;
    size_t oldRulesCount = rulesCount;

    char* oldBarFont = barFont;
    char* oldActiveBorderColor = activeBorderColor;
    char* oldInactiveBorderColor = inactiveBorderColor;
    char* oldBarBorderColor = barBorderColor;
    char* oldBarBackgroundColor = barBackgroundColor;
    char* oldBarForegroundColor = barForegroundColor;
    char* oldBarActiveWsColor = barActiveWsColor;
    char* oldBarUrgentWsColor = barUrgentWsColor;
    char* oldBarActiveTextColor = barActiveTextColor;
    char* oldBarUrgentTextColor = barUrgentTextColor;
    char* oldBarInactiveTextColor = barInactiveTextColor;
    char* oldBarStatusTextColor = barStatusTextColor;

    int oldWorkspaceCount = workspaceCount;
    float oldDefaultMasterFactor = defaultMasterFactor;
    int oldDefaultMasterCount = defaultMasterCount;
    int oldInnerGap = innerGap;
    int oldOuterGap = outerGap;
    int oldBorderWidth = borderWidth;
    int oldShowBar = showBar;
    int oldBarHeight = barHeight;
    int oldMaxTitleLength = maxTitleLength;
    int oldBarBorderWidth = barBorderWidth;
    int oldBarStrutsTop = barStrutsTop;
    int oldBarStrutsLeft = barStrutsLeft;
    int oldBarStrutsRight = barStrutsRight;

    keys = NULL;
    keysCount = 0;
    rules = NULL;
    rulesCount = 0;
    barFont = NULL;
    activeBorderColor = NULL;
    inactiveBorderColor = NULL;
    barBorderColor = NULL;
    barBackgroundColor = NULL;
    barForegroundColor = NULL;
    barActiveWsColor = NULL;
    barUrgentWsColor = NULL;
    barActiveTextColor = NULL;
    barUrgentTextColor = NULL;
    barInactiveTextColor = NULL;
    barStatusTextColor = NULL;

    int result = loadConfig();

    if (!result) {
        fprintf(stderr, "banana: failed to reload configuration, restoring old configuration\n");

        keys = oldKeys;
        keysCount = oldKeysCount;
        rules = oldRules;
        rulesCount = oldRulesCount;
        barFont = oldBarFont;
        activeBorderColor = oldActiveBorderColor;
        inactiveBorderColor = oldInactiveBorderColor;
        barBorderColor = oldBarBorderColor;
        barBackgroundColor = oldBarBackgroundColor;
        barForegroundColor = oldBarForegroundColor;
        barActiveWsColor = oldBarActiveWsColor;
        barUrgentWsColor = oldBarUrgentWsColor;
        barActiveTextColor = oldBarActiveTextColor;
        barUrgentTextColor = oldBarUrgentTextColor;
        barInactiveTextColor = oldBarInactiveTextColor;
        barStatusTextColor = oldBarStatusTextColor;
        workspaceCount = oldWorkspaceCount;
        defaultMasterFactor = oldDefaultMasterFactor;
        defaultMasterCount = oldDefaultMasterCount;
        innerGap = oldInnerGap;
        outerGap = oldOuterGap;
        borderWidth = oldBorderWidth;
        showBar = oldShowBar;
        barHeight = oldBarHeight;
        maxTitleLength = oldMaxTitleLength;
        barBorderWidth = oldBarBorderWidth;
        barStrutsTop = oldBarStrutsTop;
        barStrutsLeft = oldBarStrutsLeft;
        barStrutsRight = oldBarStrutsRight;

        return;
    }

    if (display && root) {
        XUngrabKey(display, AnyKey, AnyModifier, root);
        for (size_t i = 0; i < keysCount; i++) {
            XGrabKey(display, XKeysymToKeycode(display, keys[i].keysym), keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
        }
        XSync(display, False);
    }

    for (size_t i = 0; i < oldKeysCount; i++) {
        free((char*)oldKeys[i].arg);
    }
    free(oldKeys);

    for (size_t i = 0; i < oldRulesCount; i++) {
        free((char*)oldRules[i].className);
        free((char*)oldRules[i].instanceName);
        free((char*)oldRules[i].title);
    }
    free(oldRules);

    free(oldBarFont);
    free(oldActiveBorderColor);
    free(oldInactiveBorderColor);
    free(oldBarBorderColor);
    free(oldBarBackgroundColor);
    free(oldBarForegroundColor);
    free(oldBarActiveWsColor);
    free(oldBarUrgentWsColor);
    free(oldBarActiveTextColor);
    free(oldBarUrgentTextColor);
    free(oldBarInactiveTextColor);
    free(oldBarStatusTextColor);

    fprintf(stderr, "banana: configuration reloaded with %zu key bindings and %zu window rules\n", keysCount, rulesCount);

    if (display) {
        extern void updateBorders(void);
        extern void updateClientPositionsForBar(void);
        extern void updateClientVisibility(void);
        extern void createBars(void);
        extern void updateBars(void);
        extern void showHideBars(int show);
        extern void tileAllMonitors(void);
        extern void resetBarResources(void);
        extern int barVisible;
        extern int xerrorHandler(Display*, XErrorEvent*);

        XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

        if (!oldHandler)
            fprintf(stderr, "banana: warning - no error handler registered, proceeding with caution\n");

        XSync(display, False);
        updateBorders();
        XSync(display, False);

        resetBarResources();
        XSync(display, False);

        createBars();
        XSync(display, False);

        showHideBars(showBar);
        XSync(display, False);

        updateClientPositionsForBar();
        XSync(display, False);

        updateClientVisibility();
        XSync(display, False);

        tileAllMonitors();
        XSync(display, False);

        updateBars();
        XSync(display, False);

        XSetErrorHandler(oldHandler);
    }
}

void freeConfig(void) {
    free(barFont);
    free(activeBorderColor);
    free(inactiveBorderColor);
    free(barBorderColor);
    free(barBackgroundColor);
    free(barForegroundColor);
    free(barActiveWsColor);
    free(barUrgentWsColor);
    free(barActiveTextColor);
    free(barUrgentTextColor);
    free(barInactiveTextColor);
    free(barStatusTextColor);

    for (size_t i = 0; i < keysCount; i++) {
        free((char*)keys[i].arg);
    }
    free(keys);

    for (size_t i = 0; i < rulesCount; i++) {
        free((char*)rules[i].className);
        free((char*)rules[i].instanceName);
        free((char*)rules[i].title);
    }
    free(rules);
}