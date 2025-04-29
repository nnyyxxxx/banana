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

#define CONFIG_PATH "/.config/banana/banana.conf"
#define MAX_LINE_LENGTH 1024
#define MAX_TOKEN_LENGTH 128
#define MAX_KEYS 100
#define MAX_RULES 50

int          workspace_count = 9;
float        default_master_factor = 0.55;
int          default_master_count = 1;
int          inner_gap = 15;
int          outer_gap = 20;
int          bar_height = 20;
char*        bar_font = NULL;
int          max_title_length = 40;
int          show_bar = 1;
int          bar_border_width = 0;
int          bar_struts_top = 0;
int          bar_struts_left = 0;
int          bar_struts_right = 0;
char*        active_border_color = NULL;
char*        inactive_border_color = NULL;
char*        bar_border_color = NULL;
char*        bar_background_color = NULL;
char*        bar_foreground_color = NULL;
char*        bar_active_ws_color = NULL;
char*        bar_urgent_ws_color = NULL;
char*        bar_active_text_color = NULL;
char*        bar_urgent_text_color = NULL;
char*        bar_inactive_text_color = NULL;
char*        bar_status_text_color = NULL;
int          border_width = 2;

KeyBinding*  keys = NULL;
size_t       keys_count = 0;
WindowRule*  rules = NULL;
size_t       rules_count = 0;

static FunctionMap function_map[] = {
    {"spawn", spawnProgram},
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
    {NULL, NULL}
};

static ModifierMap modifier_map[] = {
    {"alt", Mod1Mask},
    {"shift", ShiftMask},
    {"control", ControlMask},
    {"super", Mod4Mask},
    {NULL, 0}
};

static void init_defaults(void);
static void* safe_malloc(size_t size);
static char* safe_strdup(const char* s);
static char* get_config_path(void);
static void trim(char* str);
static char** tokenize(char* line, const char* delimiter, int* count);
static KeySym get_keysym(const char* key);
static unsigned int get_modifier(const char* mod);
static void (*get_function(const char* name))(const char*);
static int parse_bind_line(char** tokens, int token_count);
static int parse_rule_line(char** tokens, int token_count);
static int parse_set_line(char** tokens, int token_count);
static void free_tokens(char** tokens, int count);

static void init_defaults(void) {
    bar_font = safe_strdup("monospace-12");
    active_border_color = safe_strdup("#6275d3");
    inactive_border_color = safe_strdup("#6275d3");
    bar_border_color = safe_strdup("#000000");
    bar_background_color = safe_strdup("#000000");
    bar_foreground_color = safe_strdup("#ced4f0");
    bar_active_ws_color = safe_strdup("#6275d3");
    bar_urgent_ws_color = safe_strdup("#6275d3");
    bar_active_text_color = safe_strdup("#000000");
    bar_urgent_text_color = safe_strdup("#000000");
    bar_inactive_text_color = safe_strdup("#ced4f0");
    bar_status_text_color = safe_strdup("#ced4f0");
}

static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "banana: failed to allocate memory\n");
        exit(1);
    }
    return ptr;
}

static char* safe_strdup(const char* s) {
    if (!s) return NULL;
    char* result = strdup(s);
    if (!result) {
        fprintf(stderr, "banana: failed to allocate memory for string\n");
        exit(1);
    }
    return result;
}

static char* get_config_path(void) {
    const char* home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "banana: HOME environment variable not set\n");
        return NULL;
    }

    char* path = safe_malloc(strlen(home) + strlen(CONFIG_PATH) + 1);
    sprintf(path, "%s%s", home, CONFIG_PATH);
    return path;
}

static void trim(char* str) {
    if (!str) return;

    char* start = str;
    while (isspace((unsigned char)*start)) start++;

    if (start != str)
        memmove(str, start, strlen(start) + 1);

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = '\0';
}

static char** tokenize(char* line, const char* delimiter, int* count) {
    if (!line || !delimiter || !count) return NULL;

    char** tokens = safe_malloc(MAX_TOKEN_LENGTH * sizeof(char*));
    *count = 0;

    char* saveptr;
    char* token = strtok_r(line, delimiter, &saveptr);

    while (token && *count < MAX_TOKEN_LENGTH) {
        char* trimmed = safe_strdup(token);
        trim(trimmed);

        if (trimmed && *trimmed)
            tokens[(*count)++] = trimmed;
        else
            free(trimmed);

        token = strtok_r(NULL, delimiter, &saveptr);
    }

    return tokens;
}

static KeySym get_keysym(const char* key) {
    if (!key) return NoSymbol;

    if (strlen(key) == 1) {
        if (key[0] >= 'a' && key[0] <= 'z')
            return XK_a + (key[0] - 'a');

        return XStringToKeysym(key);
    }

    if (strcasecmp(key, "escape") == 0) return XK_Escape;
    if (strcasecmp(key, "return") == 0) return XK_Return;
    if (strcasecmp(key, "enter") == 0) return XK_Return;
    if (strcasecmp(key, "tab") == 0) return XK_Tab;
    if (strcasecmp(key, "space") == 0) return XK_space;
    if (strcasecmp(key, "backspace") == 0) return XK_BackSpace;

    if (strcasecmp(key, "f1") == 0) return XK_F1;
    if (strcasecmp(key, "f2") == 0) return XK_F2;
    if (strcasecmp(key, "f3") == 0) return XK_F3;
    if (strcasecmp(key, "f4") == 0) return XK_F4;
    if (strcasecmp(key, "f5") == 0) return XK_F5;
    if (strcasecmp(key, "f6") == 0) return XK_F6;
    if (strcasecmp(key, "f7") == 0) return XK_F7;
    if (strcasecmp(key, "f8") == 0) return XK_F8;
    if (strcasecmp(key, "f9") == 0) return XK_F9;
    if (strcasecmp(key, "f10") == 0) return XK_F10;
    if (strcasecmp(key, "f11") == 0) return XK_F11;
    if (strcasecmp(key, "f12") == 0) return XK_F12;

    if (strcasecmp(key, "up") == 0) return XK_Up;
    if (strcasecmp(key, "down") == 0) return XK_Down;
    if (strcasecmp(key, "left") == 0) return XK_Left;
    if (strcasecmp(key, "right") == 0) return XK_Right;

    return XStringToKeysym(key);
}

static unsigned int get_modifier(const char* mod) {
    if (!mod) return 0;

    if (strchr(mod, '+')) {
        char* mod_copy = safe_strdup(mod);
        char* token = strtok(mod_copy, "+");
        unsigned int result = 0;

        while (token) {
            for (int i = 0; modifier_map[i].name; i++) {
                if (strcasecmp(token, modifier_map[i].name) == 0) {
                    result |= modifier_map[i].mask;
                    break;
                }
            }
            token = strtok(NULL, "+");
        }

        free(mod_copy);
        return result;
    }

    for (int i = 0; modifier_map[i].name; i++) {
        if (strcasecmp(mod, modifier_map[i].name) == 0)
            return modifier_map[i].mask;
    }

    return 0;
}

static void (*get_function(const char* name))(const char*) {
    if (!name) return NULL;

    for (int i = 0; function_map[i].name; i++) {
        if (strcasecmp(name, function_map[i].name) == 0)
            return function_map[i].func;
    }

    return NULL;
}

static int parse_bind_line(char** tokens, int token_count) {
    if (token_count < 4) {
        fprintf(stderr, "banana: invalid bind line, need at least 4 tokens\n");
        return 0;
    }

    const char* mod_str = tokens[1];
    const char* key_str = tokens[2];
    const char* func_str = tokens[3];
    const char* arg_str = (token_count > 4) ? tokens[4] : NULL;

    KeySym keysym = get_keysym(key_str);
    if (keysym == NoSymbol) {
        fprintf(stderr, "banana: invalid key: %s\n", key_str);
        return 0;
    }

    unsigned int mod = get_modifier(mod_str);
    if (!mod) {
        fprintf(stderr, "banana: unknown modifier: %s\n", mod_str);
        return 0;
    }

    void (*func)(const char*) = get_function(func_str);
    if (!func) {
        fprintf(stderr, "banana: unknown function: %s\n", func_str);
        return 0;
    }

    if (keys_count >= MAX_KEYS) {
        fprintf(stderr, "banana: too many key bindings (max: %d)\n", MAX_KEYS);
        return 0;
    }

    if (!keys)
        keys = safe_malloc(MAX_KEYS * sizeof(KeyBinding));

    keys[keys_count].mod = mod;
    keys[keys_count].keysym = keysym;
    keys[keys_count].func = func;
    keys[keys_count].arg = arg_str ? safe_strdup(arg_str) : NULL;

    keys_count++;
    return 1;
}

static int parse_rule_line(char** tokens, int token_count) {
    if (token_count < 3) {
        fprintf(stderr, "banana: invalid rule line, need at least 3 tokens\n");
        return 0;
    }

    if (rules_count >= MAX_RULES) {
        fprintf(stderr, "banana: too many window rules (max: %d)\n", MAX_RULES);
        return 0;
    }

    if (!rules)
        rules = safe_malloc(MAX_RULES * sizeof(WindowRule));

    rules[rules_count].className = safe_strdup(tokens[1]);
    rules[rules_count].instanceName = (token_count > 2 && strcmp(tokens[2], "*") != 0) ? safe_strdup(tokens[2]) : NULL;
    rules[rules_count].title = (token_count > 3 && strcmp(tokens[3], "*") != 0) ? safe_strdup(tokens[3]) : NULL;
    rules[rules_count].isFloating = (token_count > 4) ? atoi(tokens[4]) : -1;
    rules[rules_count].workspace = (token_count > 5) ? atoi(tokens[5]) : -1;
    rules[rules_count].monitor = (token_count > 6) ? atoi(tokens[6]) : -1;
    rules[rules_count].width = (token_count > 7) ? atoi(tokens[7]) : -1;
    rules[rules_count].height = (token_count > 8) ? atoi(tokens[8]) : -1;

    rules_count++;
    return 1;
}

static int parse_set_line(char** tokens, int token_count) {
    if (token_count < 3) {
        fprintf(stderr, "banana: invalid set line, need 3 tokens but got %d\n", token_count);
        return 0;
    }

    const char* var = tokens[1];
    const char* val = tokens[2];

    fprintf(stderr, "Setting %s to %s (token_count=%d)\n", var, val, token_count);

    if (strcmp(var, "workspace_count") == 0) workspace_count = atoi(val);
    else if (strcmp(var, "default_master_count") == 0) default_master_count = atoi(val);
    else if (strcmp(var, "inner_gap") == 0) inner_gap = atoi(val);
    else if (strcmp(var, "outer_gap") == 0) outer_gap = atoi(val);
    else if (strcmp(var, "bar_height") == 0) bar_height = atoi(val);
    else if (strcmp(var, "max_title_length") == 0) max_title_length = atoi(val);
    else if (strcmp(var, "show_bar") == 0) show_bar = atoi(val);
    else if (strcmp(var, "bar_border_width") == 0) bar_border_width = atoi(val);
    else if (strcmp(var, "bar_struts_top") == 0) bar_struts_top = atoi(val);
    else if (strcmp(var, "bar_struts_left") == 0) bar_struts_left = atoi(val);
    else if (strcmp(var, "bar_struts_right") == 0) bar_struts_right = atoi(val);
    else if (strcmp(var, "border_width") == 0) border_width = atoi(val);

    else if (strcmp(var, "default_master_factor") == 0) default_master_factor = atof(val);

    else if (strcmp(var, "bar_font") == 0) {
        free(bar_font);
        bar_font = safe_strdup(val);
    }
    else if (strcmp(var, "active_border_color") == 0) {
        free(active_border_color);
        active_border_color = safe_strdup(val);
    }
    else if (strcmp(var, "inactive_border_color") == 0) {
        free(inactive_border_color);
        inactive_border_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_border_color") == 0) {
        free(bar_border_color);
        bar_border_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_background_color") == 0) {
        free(bar_background_color);
        bar_background_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_foreground_color") == 0) {
        free(bar_foreground_color);
        bar_foreground_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_active_ws_color") == 0) {
        free(bar_active_ws_color);
        bar_active_ws_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_urgent_ws_color") == 0) {
        free(bar_urgent_ws_color);
        bar_urgent_ws_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_active_text_color") == 0) {
        free(bar_active_text_color);
        bar_active_text_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_urgent_text_color") == 0) {
        free(bar_urgent_text_color);
        bar_urgent_text_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_inactive_text_color") == 0) {
        free(bar_inactive_text_color);
        bar_inactive_text_color = safe_strdup(val);
    }
    else if (strcmp(var, "bar_status_text_color") == 0) {
        free(bar_status_text_color);
        bar_status_text_color = safe_strdup(val);
    }
    else {
        fprintf(stderr, "banana: unknown variable: %s\n", var);
        return 0;
    }

    return 1;
}

static void free_tokens(char** tokens, int count) {
    if (!tokens) return;

    for (int i = 0; i < count; i++) {
        free(tokens[i]);
    }

    free(tokens);
}

void create_default_config(void) {
    char* config_path = get_config_path();
    if (!config_path) return;

    char* dir_path = safe_strdup(config_path);
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';

        struct stat st;
        if (stat(dir_path, &st) == -1) {
            if (mkdir(dir_path, 0755) == -1) {
                fprintf(stderr, "banana: failed to create config directory: %s\n", strerror(errno));
                free(dir_path);
                free(config_path);
                return;
            }
        }
    }
    free(dir_path);

    FILE* fp = fopen(config_path, "w");
    if (!fp) {
        fprintf(stderr, "banana: failed to create config file: %s\n", strerror(errno));
        free(config_path);
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
    fprintf(fp, "bind > alt > h > adjust_master > decrease\n");
    fprintf(fp, "bind > alt > l > adjust_master > increase\n");
    fprintf(fp, "bind > alt+shift > j > move_window > down\n");
    fprintf(fp, "bind > alt+shift > k > move_window > up\n");
    fprintf(fp, "bind > alt > j > focus_window > down\n");
    fprintf(fp, "bind > alt > k > focus_window > up\n");
    fprintf(fp, "bind > alt > comma > focus_monitor > left\n");
    fprintf(fp, "bind > alt > period > focus_monitor > right\n");

    fprintf(fp, "bind > alt > 1 > switch_workspace > 0\n");
    fprintf(fp, "bind > alt > 2 > switch_workspace > 1\n");
    fprintf(fp, "bind > alt > 3 > switch_workspace > 2\n");
    fprintf(fp, "bind > alt > 4 > switch_workspace > 3\n");
    fprintf(fp, "bind > alt > 5 > switch_workspace > 4\n");
    fprintf(fp, "bind > alt > 6 > switch_workspace > 5\n");
    fprintf(fp, "bind > alt > 7 > switch_workspace > 6\n");
    fprintf(fp, "bind > alt > 8 > switch_workspace > 7\n");
    fprintf(fp, "bind > alt > 9 > switch_workspace > 8\n");

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
    fprintf(stderr, "banana: created default config file at %s\n", config_path);
    free(config_path);
}

int load_config(void) {
    init_defaults();

    free(keys);
    keys = NULL;
    keys_count = 0;

    for (size_t i = 0; i < rules_count; i++) {
        free((char*)rules[i].className);
        free((char*)rules[i].instanceName);
        free((char*)rules[i].title);
    }
    free(rules);
    rules = NULL;
    rules_count = 0;

    char* config_path = get_config_path();
    if (!config_path) return 0;

    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        fprintf(stderr, "banana: config file not found at %s, creating default\n", config_path);
        free(config_path);
        create_default_config();

        config_path = get_config_path();
        if (!config_path) return 0;

        fp = fopen(config_path, "r");
        if (!fp) {
            fprintf(stderr, "banana: failed to open config file: %s\n", strerror(errno));
            free(config_path);
            return 0;
        }
    }

    free(config_path);

    char line[MAX_LINE_LENGTH];
    char original_line[MAX_LINE_LENGTH];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        strcpy(original_line, line);

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char* comment = NULL;
        int in_color_code = 0;

        for (char* p = line; *p; p++) {
            if (*p == '#' && isxdigit((unsigned char)*(p+1)))
                continue;
            if (*p == '#' && !in_color_code) {
                comment = p;
                break;
            }
        }

        if (comment)
            *comment = '\0';

        trim(line);
        if (line[0] == '\0') continue;

        fprintf(stderr, "Line %d: '%s' (original: '%s')\n", line_num, line, original_line);

        int token_count = 0;
        char** tokens = tokenize(line, ">", &token_count);

        fprintf(stderr, "  Tokenized into %d tokens: ", token_count);
        for (int i = 0; i < token_count; i++) {
            fprintf(stderr, "'%s'%s", tokens[i], (i < token_count-1) ? ", " : "\n");
        }

        if (token_count < 2) {
            fprintf(stderr, "banana: line %d: invalid format\n", line_num);
            free_tokens(tokens, token_count);
            continue;
        }

        if (strcasecmp(tokens[0], "bind") == 0)
            parse_bind_line(tokens, token_count);
        else if (strcasecmp(tokens[0], "rule") == 0)
            parse_rule_line(tokens, token_count);
        else if (strcasecmp(tokens[0], "set") == 0)
            parse_set_line(tokens, token_count);
        else
            fprintf(stderr, "banana: line %d: unknown directive: %s\n", line_num, tokens[0]);

        free_tokens(tokens, token_count);
    }

    fclose(fp);

    fprintf(stderr, "banana: loaded %zu key bindings and %zu window rules\n", keys_count, rules_count);
    return 1;
}

void free_config(void) {
    free(bar_font);
    free(active_border_color);
    free(inactive_border_color);
    free(bar_border_color);
    free(bar_background_color);
    free(bar_foreground_color);
    free(bar_active_ws_color);
    free(bar_urgent_ws_color);
    free(bar_active_text_color);
    free(bar_urgent_text_color);
    free(bar_inactive_text_color);
    free(bar_status_text_color);

    for (size_t i = 0; i < keys_count; i++) {
        free((char*)keys[i].arg);
    }
    free(keys);

    for (size_t i = 0; i < rules_count; i++) {
        free((char*)rules[i].className);
        free((char*)rules[i].instanceName);
        free((char*)rules[i].title);
    }
    free(rules);
}