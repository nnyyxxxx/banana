#ifndef CONFIG_H
#define CONFIG_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stddef.h>

#define CONFIG_PATH	 "/.config/banana/banana.conf"
#define MAX_LINE_LENGTH	 1024
#define MAX_TOKEN_LENGTH 128
#define MAX_KEYS	 100
#define MAX_RULES	 50
#define MAX_ERRORS	 100
#define MAX_VARIABLES	 50
#define MAX_AUTOSTARTS	 50
#define MAX_SECTIONS	 20

#define SECTION_GENERAL	   "general"
#define SECTION_BAR	   "bar"
#define SECTION_DECORATION "decoration"
#define SECTION_BINDS	   "binds"
#define SECTION_RULES	   "rules"
#define SECTION_MASTER	   "master"

typedef struct {
	unsigned int mod;
	KeySym	     keysym;
	void (*func)(const char *);
	const char *arg;
} SKeyBinding;

typedef struct {
	const char *className;
	const char *instanceName;
	const char *title;
	int	    isFloating;
	int	    workspace;
	int	    monitor;
	int	    width;
	int	    height;
	int	    swallowing;
	int	    noswallow;
} SWindowRule;

typedef struct {
	char *name;
	char *value;
} SVariable;

typedef struct {
	const char *name;
	void (*func)(const char *);
} SFunctionMap;

typedef struct {
	const char  *name;
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
	int	     count;
} SConfigErrors;

typedef enum {
	TOKEN_HANDLER_LOAD,
	TOKEN_HANDLER_VALIDATE
} ETokenHandlerMode;

typedef struct {
	char sectionName[MAX_TOKEN_LENGTH];
	int  startLine;
	int  lastContentLine;
	int  isDuplicate;
	int  isInvalid;
} SSectionInfo;

typedef struct {
	char sectionNames[MAX_SECTIONS][MAX_TOKEN_LENGTH];
	int  sectionLines[MAX_SECTIONS];
	int  count;
} SSeenSections;

typedef struct {
	ETokenHandlerMode mode;
	SConfigErrors	 *errors;
	int		  hasErrors;
	SSeenSections	  seenSections;
} STokenHandlerContext;

typedef struct {
	char *command;
} SAutostart;

void	     spawnProgram(const char *arg);
void	     killClient(const char *arg);
void	     quit(const char *arg);
void	     switchToWorkspace(const char *arg);
void	     moveClientToWorkspace(const char *arg);
void	     toggleFloating(const char *arg);
void	     toggleFullscreen(const char *arg);
void	     moveWindowInStack(const char *arg);
void	     focusWindowInStack(const char *arg);
void	     adjustMasterFactor(const char *arg);
void	     focusMonitor(const char *arg);
void	     toggleBar(const char *arg);
void	     cycleLayouts(const char *arg);
void	     reloadConfig(const char *arg);
char	    *safeStrdup(const char *s);
int	     loadConfig(void);
int	     validateConfig(SConfigErrors *errors);
void	     printConfigErrors(SConfigErrors *errors);

void	     initDefaults(void);
void	    *safeMalloc(size_t size);
char	    *getConfigPath(void);
void	     trim(char *str);
KeySym	     getKeysym(const char *key);
unsigned int getModifier(const char *mod);
void (*getFunction(const char *name))(const char *);
void   freeTokens(char **tokens, int count);
void   addError(SConfigErrors *errors, const char *message, int lineNum,
		int isFatal);

int    isValidInteger(const char *str);
int    isValidFloat(const char *str);
int    isValidWorkspaceIndex(const char *arg);
int    isValidAdjustMasterArg(const char *arg);
int    isValidMoveWindowArg(const char *arg);
int    isValidFocusWindowArg(const char *arg);
int    isValidFocusMonitorArg(const char *arg);
int    isValidHexColor(const char *str);

char **tokenizeLine(const char *line, int *tokenCount);
int    initializeConfig(STokenHandlerContext *ctx, SKeyBinding **oldKeys,
			size_t *oldKeysCount, SWindowRule **oldRules,
			size_t *oldRulesCount);
int    handleGeneralSection(STokenHandlerContext *ctx, const char *var,
			    const char *val, int lineNum, char **tokens,
			    int tokenCount);
int    handleBarSection(STokenHandlerContext *ctx, const char *var,
			const char *val, int lineNum, char **tokens,
			int tokenCount);
int    handleDecorationSection(STokenHandlerContext *ctx, const char *var,
			       const char *val, int lineNum, char **tokens,
			       int tokenCount);
int    handleBindsSection(STokenHandlerContext *ctx, const char *modStr,
			  const char *keyStr, const char *funcStr,
			  const char *argStr, int lineNum, char **tokens,
			  int tokenCount);
int handleRulesSection(STokenHandlerContext *ctx, int tokenCount, char **tokens,
		       int lineNum);
int handleMasterSection(STokenHandlerContext *ctx, const char *var,
			const char *val, int lineNum, char **tokens,
			int tokenCount);
int reportBraceMismatch(STokenHandlerContext *ctx, int sectionDepth,
			SSectionInfo *sectionStack);

void  backupConfigState(SKeyBinding **oldKeys, size_t *oldKeysCount,
			SWindowRule **oldRules, size_t *oldRulesCount);
void  restoreConfigState(SKeyBinding *oldKeys, size_t oldKeysCount,
			 SWindowRule *oldRules, size_t oldRulesCount);
FILE *openConfigFile(STokenHandlerContext *ctx, char **configPath,
		     SKeyBinding *oldKeys, size_t oldKeysCount,
		     SWindowRule *oldRules, size_t oldRulesCount);
int   processConfigFile(FILE *fp, STokenHandlerContext *ctx, int *braceDepth,
			int *sectionDepth, SSectionInfo *sectionStack,
			int *potentialSectionLineNum, char *potentialSectionName);
int   processLine(const char *line, char *section, int *inSection,
		  int *braceDepth, int *potentialSectionLineNum,
		  char *potentialSectionName, SSectionInfo *sectionStack,
		  int *sectionDepth, int lineNum, STokenHandlerContext *ctx);
int   finalizeConfigParser(STokenHandlerContext *ctx, SKeyBinding *oldKeys,
			   size_t oldKeysCount, SWindowRule *oldRules,
			   size_t oldRulesCount, int braceDepth, int sectionDepth,
			   SSectionInfo *sectionStack,
			   int		 potentialSectionLineNum,
			   char		*potentialSectionName);
void  cleanupConfigData(void);

int   processConfigVariable(const char *name, const char *value, int lineNum,
			    STokenHandlerContext *ctx);
int   processExecCommand(const char *command, int lineNum,
			 STokenHandlerContext *ctx);
char *substituteVariables(const char *str);
const char     *getVariableValue(const char *name);
void		cleanupVariables(void);
void		runAutostart(void);
void		cleanupAutostart(void);

extern Display *display;
extern Window	root;

extern int	workspaceCount;
extern float	defaultMasterFactor;
extern int	innerGap;
extern int	outerGap;
#define modkey Mod1Mask
extern int		  barHeight;
extern char		 *barFont;
extern int		  showBar;
extern int		  bottomBar;
extern int		  barBorderWidth;
extern int		  barStrutsTop;
extern int		  barStrutsLeft;
extern int		  barStrutsRight;
extern int		  showOnlyActiveWorkspaces;
extern char		 *activeBorderColor;
extern char		 *inactiveBorderColor;
extern char		 *barBorderColor;
extern char		 *barBackgroundColor;
extern char		 *barForegroundColor;
extern char		 *barActiveWsColor;
extern char		 *barUrgentWsColor;
extern char		 *barActiveTextColor;
extern char		 *barUrgentTextColor;
extern char		 *barInactiveTextColor;
extern char		 *barStatusTextColor;
extern int		  borderWidth;
extern char		 *terminal;
extern char		 *launcher;
extern char		 *wall;
extern char		 *screenshot;
extern int		  newAsMaster;

extern int		  showErrorNotifications;
extern char		 *errorBorderColor;
extern int		  errorBorderWidth;
extern char		 *errorBackgroundColor;
extern char		 *errorTextColor;
extern char		 *errorFont;

extern char		 *defaultLayout;
extern int		  no_warps;
extern int		  forcedMonitor;

extern SKeyBinding	 *keys;
extern size_t		  keysCount;
extern SWindowRule	 *rules;
extern size_t		  rulesCount;
extern SAutostart	 *autostarts;
extern size_t		  autostartsCount;

extern const SFunctionMap functionMap[];
extern const SModifierMap modifierMap[];

extern SVariable	 *variables;
extern size_t		  variablesCount;

int			  parseConfigFile(STokenHandlerContext *ctx);
int			  loadConfig(void);
void			  createDefaultConfig(void);
void			  freeConfig(void);

#endif /* CONFIG_H */
