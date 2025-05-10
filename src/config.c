#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/keysym.h>

#include "banana.h"
#include "config.h"
#include "bar.h"

extern int	   barVisible;

int		   workspaceCount	    = 9;
float		   defaultMasterFactor	    = 0.55;
int		   innerGap		    = 15;
int		   outerGap		    = 20;
int		   smartGaps		    = 0;
int		   barHeight		    = 20;
char		  *barFont		    = NULL;
int		   showBar		    = 1;
int		   bottomBar		    = 0;
int		   showOnlyActiveWorkspaces = 0;
int		   barBorderWidth	    = 0;
int		   barStrutsTop		    = 0;
int		   barStrutsLeft	    = 0;
int		   barStrutsRight	    = 0;
char		  *activeBorderColor	    = NULL;
char		  *inactiveBorderColor	    = NULL;
char		  *barBorderColor	    = NULL;
char		  *barBackgroundColor	    = NULL;
char		  *barForegroundColor	    = NULL;
char		  *barActiveWsColor	    = NULL;
char		  *barUrgentWsColor	    = NULL;
char		  *barActiveTextColor	    = NULL;
char		  *barUrgentTextColor	    = NULL;
char		  *barInactiveTextColor	    = NULL;
char		  *barStatusTextColor	    = NULL;
int		   borderWidth		    = 2;
int		   newAsMaster		    = 0;
int		   centeredMaster	    = 0;
char		  *defaultLayout	    = NULL;
int		   no_warps		    = 0;

SKeyBinding	  *keys	      = NULL;
size_t		   keysCount  = 0;
SWindowRule	  *rules      = NULL;
size_t		   rulesCount = 0;

SVariable	  *variables	  = NULL;
size_t		   variablesCount = 0;

SAutostart	  *autostarts	   = NULL;
size_t		   autostartsCount = 0;

const SFunctionMap functionMap[] = {
    {"spawn", spawnProgram},
    {"kill", killClient},
    {"quit", quit},
    {"switch_workspace", switchToWorkspace},
    {"move_to_workspace", moveClientToWorkspace},
    {"toggle_floating", toggleFloating},
    {"toggle_fullscreen", toggleFullscreen},
    {"move_window", moveWindowInStack},
    {"resize_window", resizeWindowKeyboard},
    {"focus_window", focusWindowInStack},
    {"cycle_focus", cycleFocusBetweenFloatingAndMonocle},
    {"adjust_master", adjustMasterFactor},
    {"focus_monitor", focusMonitor},
    {"toggle_bar", toggleBar},
    {"reload_config", reloadConfig},
    {"cycle_layouts", cycleLayouts},
    {NULL, NULL}};

const SModifierMap modifierMap[] = {{"alt", Mod1Mask},
				    {"shift", ShiftMask},
				    {"control", ControlMask},
				    {"super", Mod4Mask},
				    {NULL, 0}};

int		   isValidWorkspaceIndex(const char *arg)
{
	if (!isValidInteger(arg)) {
		return 0;
	}

	int value = atoi(arg);
	return (value >= 0 && value < workspaceCount);
}

int isValidAdjustMasterArg(const char *arg)
{
	return (strcmp(arg, "increase") == 0 || strcmp(arg, "decrease") == 0);
}

int isValidMoveWindowArg(const char *arg)
{
	return (strcmp(arg, "up") == 0 || strcmp(arg, "down") == 0 ||
		strcmp(arg, "left") == 0 || strcmp(arg, "right") == 0);
}

int isValidFocusWindowArg(const char *arg)
{
	return (strcmp(arg, "up") == 0 || strcmp(arg, "down") == 0);
}

int isValidCycleFocusArg(const char *arg)
{
	return (strcmp(arg, "up") == 0 || strcmp(arg, "down") == 0);
}

int isValidFocusMonitorArg(const char *arg)
{
	return (strcmp(arg, "left") == 0 || strcmp(arg, "right") == 0);
}

int isValidInteger(const char *str)
{
	if (!str || *str == '\0') {
		return 0;
	}

	if (*str == '-') {
		str++;
	}

	if (*str == '\0') {
		return 0;
	}

	while (*str) {
		if (!isdigit((unsigned char)*str)) {
			return 0;
		}
		str++;
	}

	return 1;
}

int isValidFloat(const char *str)
{
	if (!str || *str == '\0') {
		return 0;
	}

	if (*str == '-') {
		str++;
	}

	if (*str == '\0') {
		return 0;
	}

	int decimal_point = 0;

	while (*str) {
		if (*str == '.') {
			if (decimal_point) {
				return 0;
			}
			decimal_point = 1;
		} else if (!isdigit((unsigned char)*str)) {
			return 0;
		}
		str++;
	}

	return 1;
}

int isValidHexColor(const char *str)
{
	if (!str || *str == '\0') {
		return 0;
	}

	if (str[0] != '#') {
		return 0;
	}

	str++;

	size_t len = strlen(str);
	if (len != 6) {
		return 0;
	}

	while (*str) {
		if (!isxdigit((unsigned char)*str)) {
			return 0;
		}
		str++;
	}

	return 1;
}

int isValidResizeWindowArg(const char *arg)
{
	return (strcmp(arg, "up") == 0 || strcmp(arg, "down") == 0 ||
		strcmp(arg, "left") == 0 || strcmp(arg, "right") == 0 ||
		strcmp(arg, "grow_up") == 0 || strcmp(arg, "grow_down") == 0 ||
		strcmp(arg, "grow_left") == 0 ||
		strcmp(arg, "grow_right") == 0);
}

char **tokenizeLine(const char *line, int *tokenCount)
{
	if (!line || !line[0]) {
		*tokenCount = 0;
		return NULL;
	}

	char **tokens = safeMalloc(MAX_TOKEN_LENGTH * sizeof(char *));
	*tokenCount   = 0;

	char *lineClone = safeStrdup(line);
	if (!lineClone) {
		free(tokens);
		*tokenCount = 0;
		return NULL;
	}

	char	   *p		      = lineClone;
	int	    inDoubleQuote     = 0;
	int	    inSingleQuote     = 0;
	char	   *tokenStart	      = p;
	int	    hasUnmatchedQuote = 0;

	const char *check	= line;
	int	    dquoteCount = 0;
	int	    squoteCount = 0;

	while (*check) {
		if (*check == '\"') {
			dquoteCount++;
		} else if (*check == '\'') {
			squoteCount++;
		}
		check++;
	}

	if (dquoteCount % 2 != 0 || squoteCount % 2 != 0) {
		hasUnmatchedQuote = 1;
	}

	while (*p) {
		if (*p == '\"' && !inSingleQuote) {
			inDoubleQuote = !inDoubleQuote;
		} else if (*p == '\'' && !inDoubleQuote) {
			inSingleQuote = !inSingleQuote;
		} else if (isspace((unsigned char)*p) && !inDoubleQuote &&
			   !inSingleQuote) {
			*p = '\0';
			if (p > tokenStart && *(p - 1) != '\0') {
				char *t		  = tokenStart;
				int   dquoteCount = 0;
				int   squoteCount = 0;

				while (t < p) {
					if (*t == '\"') {
						dquoteCount++;
					} else if (*t == '\'') {
						squoteCount++;
					}
					t++;
				}

				if (dquoteCount % 2 != 0 ||
				    squoteCount % 2 != 0) {
					hasUnmatchedQuote = 1;
				}

				if (tokenStart[0] == '\"' && *(p - 1) == '\"') {
					tokenStart++;
					*(p - 1) = '\0';
				} else if (tokenStart[0] == '\'' &&
					   *(p - 1) == '\'') {
					tokenStart++;
					*(p - 1) = '\0';
				}

				if (strchr(tokenStart, '$') != NULL &&
				    variables != NULL && variablesCount > 0) {
					char *substituted =
					    substituteVariables(tokenStart);
					if (substituted) {
						tokens[(*tokenCount)++] =
						    substituted;
					} else {
						tokens[(*tokenCount)++] =
						    safeStrdup(tokenStart);
					}
				} else {
					tokens[(*tokenCount)++] =
					    safeStrdup(tokenStart);
				}
			}
			tokenStart = p + 1;
		}
		p++;
	}

	if (p > tokenStart && *(p - 1) != '\0') {
		char *t		  = tokenStart;
		int   dquoteCount = 0;
		int   squoteCount = 0;

		while (t < p) {
			if (*t == '\"') {
				dquoteCount++;
			} else if (*t == '\'') {
				squoteCount++;
			}
			t++;
		}

		if (dquoteCount % 2 != 0 || squoteCount % 2 != 0) {
			hasUnmatchedQuote = 1;
		}

		if (tokenStart[0] == '\"' && *(p - 1) == '\"') {
			tokenStart++;
			*(p - 1) = '\0';
		} else if (tokenStart[0] == '\'' && *(p - 1) == '\'') {
			tokenStart++;
			*(p - 1) = '\0';
		}

		if (strchr(tokenStart, '$') != NULL && variables != NULL &&
		    variablesCount > 0) {
			char *substituted = substituteVariables(tokenStart);
			if (substituted) {
				tokens[(*tokenCount)++] = substituted;
			} else {
				tokens[(*tokenCount)++] =
				    safeStrdup(tokenStart);
			}
		} else {
			tokens[(*tokenCount)++] = safeStrdup(tokenStart);
		}
	}

	if (inDoubleQuote || inSingleQuote) {
		hasUnmatchedQuote = 1;
	}

	if (hasUnmatchedQuote && *tokenCount > 0) {
		char *lastToken = tokens[*tokenCount - 1];
		if (lastToken) {
			char *flaggedToken = safeMalloc(strlen(lastToken) + 20);
			sprintf(flaggedToken, "%s__UNMATCHED_QUOTE__",
				lastToken);
			free(lastToken);
			tokens[*tokenCount - 1] = flaggedToken;
		}
	}

	free(lineClone);

	if (*tokenCount == 0) {
		free(tokens);
		return NULL;
	}

	return tokens;
}

int parseConfigFile(STokenHandlerContext *ctx)
{
	initDefaults();

	SKeyBinding *oldKeys	   = NULL;
	size_t	     oldKeysCount  = 0;
	SWindowRule *oldRules	   = NULL;
	size_t	     oldRulesCount = 0;

	backupConfigState(&oldKeys, &oldKeysCount, &oldRules, &oldRulesCount);

	char *configPath = NULL;
	FILE *fp = openConfigFile(ctx, &configPath, oldKeys, oldKeysCount,
				  oldRules, oldRulesCount);
	if (!fp) {
		if (configPath) {
			free(configPath);
		}
		return (ctx->mode == TOKEN_HANDLER_VALIDATE) ? 1 : 0;
	}

	if (configPath) {
		free(configPath);
	}

	int	     braceDepth				    = 0;
	int	     sectionDepth			    = 0;
	SSectionInfo sectionStack[10]			    = {0};
	int	     potentialSectionLineNum		    = 0;
	char	     potentialSectionName[MAX_TOKEN_LENGTH] = "";

	processConfigFile(fp, ctx, &braceDepth, &sectionDepth, sectionStack,
			  &potentialSectionLineNum, potentialSectionName);
	fclose(fp);

	return finalizeConfigParser(ctx, oldKeys, oldKeysCount, oldRules,
				    oldRulesCount, braceDepth, sectionDepth,
				    sectionStack, potentialSectionLineNum,
				    potentialSectionName);
}

void backupConfigState(SKeyBinding **oldKeys, size_t *oldKeysCount,
		       SWindowRule **oldRules, size_t *oldRulesCount)
{
	*oldKeys       = keys;
	*oldKeysCount  = keysCount;
	*oldRules      = rules;
	*oldRulesCount = rulesCount;

	keys	   = NULL;
	keysCount  = 0;
	rules	   = NULL;
	rulesCount = 0;
}

void restoreConfigState(SKeyBinding *oldKeys, size_t oldKeysCount,
			SWindowRule *oldRules, size_t oldRulesCount)
{
	cleanupConfigData();

	keys	   = oldKeys;
	keysCount  = oldKeysCount;
	rules	   = oldRules;
	rulesCount = oldRulesCount;
}

FILE *openConfigFile(STokenHandlerContext *ctx, char **configPath,
		     SKeyBinding *oldKeys, size_t oldKeysCount,
		     SWindowRule *oldRules, size_t oldRulesCount)
{
	*configPath = getConfigPath();
	if (!*configPath) {
		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors,
				 "HOME environment variable not set", 0, 1);
		} else {
			fprintf(stderr, "banana: HOME environment variable not "
					"set\n");
		}

		restoreConfigState(oldKeys, oldKeysCount, oldRules,
				   oldRulesCount);
		return NULL;
	}

	FILE *fp = fopen(*configPath, "r");
	if (!fp) {
		if (errno == ENOENT) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors,
					 "Config file not found, would create "
					 "default",
					 0, 0);
			} else {
				fprintf(stderr,
					"banana: config file not found at %s, "
					"creating default\n",
					*configPath);
				free(*configPath);
				createDefaultConfig();

				*configPath = getConfigPath();
				if (!*configPath) {
					restoreConfigState(oldKeys,
							   oldKeysCount,
							   oldRules,
							   oldRulesCount);
					return NULL;
				}

				fp = fopen(*configPath, "r");
				if (!fp) {
					fprintf(stderr,
						"banana: failed to open config "
						"file: %s\n",
						strerror(errno));
					restoreConfigState(oldKeys,
							   oldKeysCount,
							   oldRules,
							   oldRulesCount);
					return NULL;
				}
			}
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Failed to open config file: %s",
				 strerror(errno));

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, 0, 1);
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}

			restoreConfigState(oldKeys, oldKeysCount, oldRules,
					   oldRulesCount);
			return NULL;
		}

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			restoreConfigState(oldKeys, oldKeysCount, oldRules,
					   oldRulesCount);
			return NULL;
		}
	}

	return fp;
}

int processConfigFile(FILE *fp, STokenHandlerContext *ctx, int *braceDepth,
		      int *sectionDepth, SSectionInfo *sectionStack,
		      int *potentialSectionLineNum, char *potentialSectionName)
{
	char line[MAX_LINE_LENGTH];
	char section[MAX_TOKEN_LENGTH] = "";
	int  inSection		       = 0;
	int  lineNum		       = 0;

	while (fgets(line, sizeof(line), fp)) {
		lineNum++;

		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
			continue;
		}

		char *comment	    = NULL;
		int   inColorCode   = 0;
		int   inDoubleQuote = 0;
		int   inSingleQuote = 0;

		for (char *p = line; *p; p++) {
			if (*p == '\"' && !inSingleQuote) {
				inDoubleQuote = !inDoubleQuote;
			} else if (*p == '\'' && !inDoubleQuote) {
				inSingleQuote = !inSingleQuote;
			}

			if (*p == '#' && isxdigit((unsigned char)*(p + 1))) {
				inColorCode = 1;
			} else if (isspace((unsigned char)*p)) {
				inColorCode = 0;
			}

			if (*p == '#' && !inColorCode && !inDoubleQuote &&
			    !inSingleQuote) {
				comment = p;
				break;
			}
		}

		if (comment) {
			*comment = '\0';
		}

		trim(line);
		if (line[0] == '\0') {
			continue;
		}

		int result = processLine(line, section, &inSection, braceDepth,
					 potentialSectionLineNum,
					 potentialSectionName, sectionStack,
					 sectionDepth, lineNum, ctx);
		if (result == 0) {
			continue;
		}
	}

	return 1;
}

int processLine(const char *line, char *section, int *inSection,
		int *braceDepth, int *potentialSectionLineNum,
		char *potentialSectionName, SSectionInfo *sectionStack,
		int *sectionDepth, int lineNum, STokenHandlerContext *ctx)
{
	int hasOpeningBrace = 0;

	{
		int inDoubleQuote = 0;
		int inSingleQuote = 0;

		for (int i = 0; line[i]; i++) {
			if (line[i] == '\"' && !inSingleQuote) {
				inDoubleQuote = !inDoubleQuote;
			} else if (line[i] == '\'' && !inDoubleQuote) {
				inSingleQuote = !inSingleQuote;
			} else if (line[i] == '{' && !inDoubleQuote &&
				   !inSingleQuote) {
				hasOpeningBrace = 1;
				break;
			}
		}
	}

	if (hasOpeningBrace) {
		char sectionName[MAX_TOKEN_LENGTH] = "";
		if (sscanf(line, "%s {", sectionName) == 1) {
			int isInvalid = 0;
			if (strcasecmp(sectionName, SECTION_GENERAL) != 0 &&
			    strcasecmp(sectionName, SECTION_BAR) != 0 &&
			    strcasecmp(sectionName, SECTION_DECORATION) != 0 &&
			    strcasecmp(sectionName, SECTION_BINDS) != 0 &&
			    strcasecmp(sectionName, SECTION_RULES) != 0 &&
			    strcasecmp(sectionName, SECTION_MASTER) != 0) {
				isInvalid = 1;
				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Unknown section: %s",
						 sectionName);
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr,
						"banana: Unknown section: %s\n",
						sectionName);
				}
			}

			int isDuplicate = 0;

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				for (int i = 0; i < ctx->seenSections.count;
				     i++) {
					if (strcasecmp(ctx->seenSections
							   .sectionNames[i],
						       sectionName) == 0) {
						char errMsg[MAX_LINE_LENGTH];
						snprintf(errMsg,
							 MAX_LINE_LENGTH,
							 "Duplicate section: "
							 "%s "
							 "(previously defined "
							 "at "
							 "line %d)",
							 sectionName,
							 ctx->seenSections
							     .sectionLines[i]);
						addError(ctx->errors, errMsg,
							 lineNum, 0);
						ctx->hasErrors = 1;
						isDuplicate    = 1;
						break;
					}
				}

				if (ctx->seenSections.count < MAX_SECTIONS) {
					strcpy(ctx->seenSections.sectionNames
						   [ctx->seenSections.count],
					       sectionName);
					ctx->seenSections.sectionLines
					    [ctx->seenSections.count] = lineNum;
					ctx->seenSections.count++;
				}
			}

			strcpy(section, sectionName);
			*inSection = 1;
			(*braceDepth)++;

			if (*sectionDepth < 10) {
				strcpy(sectionStack[*sectionDepth].sectionName,
				       sectionName);
				sectionStack[*sectionDepth].startLine = lineNum;
				sectionStack[*sectionDepth].lastContentLine =
				    lineNum;
				sectionStack[*sectionDepth].isDuplicate =
				    isDuplicate;
				sectionStack[*sectionDepth].isInvalid =
				    isInvalid;
				(*sectionDepth)++;
			}

			*potentialSectionLineNum = 0;
			potentialSectionName[0]	 = '\0';
		} else {
			(*braceDepth)++;
		}

		int inDoubleQuote   = 0;
		int inSingleQuote   = 0;
		int foundOpenBrace  = 0;
		int foundCloseBrace = 0;

		for (int i = 0; line[i]; i++) {
			if (line[i] == '\"' && !inSingleQuote) {
				inDoubleQuote = !inDoubleQuote;
			} else if (line[i] == '\'' && !inDoubleQuote) {
				inSingleQuote = !inSingleQuote;
			} else if (line[i] == '{' && !inDoubleQuote &&
				   !inSingleQuote) {
				foundOpenBrace = 1;
			} else if (line[i] == '}' && !inDoubleQuote &&
				   !inSingleQuote && foundOpenBrace) {
				foundCloseBrace = 1;
			}
		}

		if (foundOpenBrace && foundCloseBrace) {
			(*braceDepth)--;
			if (*braceDepth == 0) {
				*inSection = 0;
				section[0] = '\0';
			}

			if (*sectionDepth > 0) {
				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					SSectionInfo *currentSection =
					    &sectionStack[*sectionDepth - 1];
					if (currentSection->startLine ==
						lineNum &&
					    currentSection->lastContentLine ==
						lineNum &&
					    !currentSection->isDuplicate &&
					    !currentSection->isInvalid) {
						char errMsg[MAX_LINE_LENGTH];
						snprintf(
						    errMsg, MAX_LINE_LENGTH,
						    "Empty section: %s",
						    currentSection->sectionName);
						addError(ctx->errors, errMsg,
							 lineNum, 0);
						ctx->hasErrors = 1;
					}
				}
				(*sectionDepth)--;
			}
		}

		return 1;
	}

	int hasClosingBrace = 0;
	{
		int inDoubleQuote = 0;
		int inSingleQuote = 0;

		for (int i = 0; line[i]; i++) {
			if (line[i] == '\"' && !inSingleQuote) {
				inDoubleQuote = !inDoubleQuote;
			} else if (line[i] == '\'' && !inDoubleQuote) {
				inSingleQuote = !inSingleQuote;
			} else if (line[i] == '}' && !inDoubleQuote &&
				   !inSingleQuote) {
				hasClosingBrace = 1;
				break;
			}
		}
	}

	if (hasClosingBrace) {
		(*braceDepth)--;
		if (*braceDepth < 0) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Unexpected closing brace - no "
					 "matching opening brace");
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				*braceDepth    = 0;
			}
		} else if (*braceDepth == 0) {
			*inSection = 0;
			section[0] = '\0';
		}

		if (*sectionDepth > 0) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				SSectionInfo *currentSection =
				    &sectionStack[*sectionDepth - 1];
				if (currentSection->startLine ==
					currentSection->lastContentLine &&
				    !currentSection->isDuplicate &&
				    !currentSection->isInvalid) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Empty section: %s",
						 currentSection->sectionName);
					addError(ctx->errors, errMsg,
						 currentSection->startLine, 0);
					ctx->hasErrors = 1;
				}
			}
			(*sectionDepth)--;
		}

		return 1;
	}

	if (*inSection && *sectionDepth > 0) {
		sectionStack[*sectionDepth - 1].lastContentLine = lineNum;
	}

	if (!*inSection) {
		if (!hasOpeningBrace && !hasClosingBrace) {
			int    tokenCount = 0;
			char **tokens	  = tokenizeLine(line, &tokenCount);

			if (tokens && tokenCount > 0) {
				char *lastToken = tokens[tokenCount - 1];
				if (strstr(lastToken, "__UNMATCHED_QUOTE__") !=
				    NULL) {
					if (ctx->mode ==
					    TOKEN_HANDLER_VALIDATE) {
						char errMsg[MAX_LINE_LENGTH];
						snprintf(errMsg,
							 MAX_LINE_LENGTH,
							 "Unmatched quotes in "
							 "line");
						addError(ctx->errors, errMsg,
							 lineNum, 0);
						ctx->hasErrors = 1;
					}

					char *marker = strstr(lastToken, "__"
									 "UNMAT"
									 "CHED_"
									 "QUOTE"
									 "__");
					if (marker) {
						*marker = '\0';
					}
				}
			}

			if (tokens && tokenCount == 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Stray text not assigned to anything: "
					 "'%s'",
					 tokens[0]);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}

			if (tokens && tokenCount >= 2) {
				if (strcasecmp(tokens[0], "exec") == 0) {
					char command[MAX_LINE_LENGTH] = "";
					for (int i = 1; i < tokenCount; i++) {
						if (i > 1) {
							strcat(command, " ");
						}
						strcat(command, tokens[i]);
					}

					if ((command[0] == '"' &&
					     command[strlen(command) - 1] ==
						 '"') ||
					    (command[0] == '\'' &&
					     command[strlen(command) - 1] ==
						 '\'')) {
						command[strlen(command) - 1] =
						    '\0';
						memmove(command, command + 1,
							strlen(command));
					}

					processExecCommand(command, lineNum,
							   ctx);
					freeTokens(tokens, tokenCount);
					return 1;
				} else if (strcasecmp(tokens[0],
						      SECTION_GENERAL) != 0 &&
					   strcasecmp(tokens[0], SECTION_BAR) !=
					       0 &&
					   strcasecmp(tokens[0],
						      SECTION_DECORATION) !=
					       0 &&
					   strcasecmp(tokens[0],
						      SECTION_BINDS) != 0 &&
					   strcasecmp(tokens[0],
						      SECTION_RULES) != 0 &&
					   strcasecmp(tokens[0],
						      SECTION_MASTER) != 0) {
					char value[MAX_LINE_LENGTH] = "";
					for (int i = 1; i < tokenCount; i++) {
						if (i > 1) {
							strcat(value, " ");
						}
						strcat(value, tokens[i]);
					}

					processConfigVariable(tokens[0], value,
							      lineNum, ctx);
					freeTokens(tokens, tokenCount);
					return 1;
				}
				freeTokens(tokens, tokenCount);
			} else if (tokens) {
				freeTokens(tokens, tokenCount);
			}
		}

		int hasSpace	 = 0;
		int hasOpenBrace = 0;
		{
			int inDoubleQuote = 0;
			int inSingleQuote = 0;

			for (int i = 0; line[i]; i++) {
				if (line[i] == '\"' && !inSingleQuote) {
					inDoubleQuote = !inDoubleQuote;
				} else if (line[i] == '\'' && !inDoubleQuote) {
					inSingleQuote = !inSingleQuote;
				} else if (isspace((unsigned char)line[i]) &&
					   !inDoubleQuote && !inSingleQuote) {
					hasSpace = 1;
				} else if (line[i] == '{' && !inDoubleQuote &&
					   !inSingleQuote) {
					hasOpenBrace = 1;
				}
			}
		}

		if (hasSpace && !hasOpenBrace) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				if (*potentialSectionLineNum > 0) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Missing opening brace after "
						 "section name '%s'",
						 potentialSectionName);
					addError(ctx->errors, errMsg,
						 *potentialSectionLineNum, 0);

					*potentialSectionLineNum = 0;
					potentialSectionName[0]	 = '\0';
				}

				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Content outside of section - missing "
					 "opening brace");
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			}
		} else if (!hasSpace && !hasOpenBrace) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				int dquoteCount = 0;
				int squoteCount = 0;
				for (int i = 0; line[i]; i++) {
					if (line[i] == '\"') {
						dquoteCount++;
					} else if (line[i] == '\'') {
						squoteCount++;
					}
				}

				if (dquoteCount % 2 != 0 ||
				    squoteCount % 2 != 0) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Unmatched quotes in line");
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				}

				strncpy(potentialSectionName, line,
					MAX_TOKEN_LENGTH - 1);
				potentialSectionName[MAX_TOKEN_LENGTH - 1] =
				    '\0';
				trim(potentialSectionName);
				*potentialSectionLineNum = lineNum;
			}
		} else if (hasOpenBrace) {
			*potentialSectionLineNum = 0;
			potentialSectionName[0]	 = '\0';
		}
		return 1;
	}

	int    tokenCount;
	char **tokens = tokenizeLine(line, &tokenCount);

	if (!tokens) {
		return 0;
	}

	if (tokenCount > 0) {
		char *lastToken = tokens[tokenCount - 1];
		if (strstr(lastToken, "__UNMATCHED_QUOTE__") != NULL) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Unmatched quotes in line");
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			}

			char *marker = strstr(lastToken, "__UNMATCHED_QUOTE__");
			if (marker) {
				*marker = '\0';
			}
		}
	}

	if (tokenCount < 2) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Invalid format (needs at least 2 tokens)");

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: line %d: %s\n", lineNum,
				errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 0;
	}

	int handlerFreedTokens = 0;

	if (strcasecmp(section, SECTION_GENERAL) == 0) {
		const char *var = tokens[0];
		const char *val = tokens[1];

		if (handleGeneralSection(ctx, var, val, lineNum, tokens,
					 tokenCount)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else if (strcasecmp(section, SECTION_BAR) == 0) {
		const char *var = tokens[0];
		const char *val = tokens[1];

		if (handleBarSection(ctx, var, val, lineNum, tokens,
				     tokenCount)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else if (strcasecmp(section, SECTION_DECORATION) == 0) {
		const char *var = tokens[0];
		const char *val = tokens[1];

		if (handleDecorationSection(ctx, var, val, lineNum, tokens,
					    tokenCount)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else if (strcasecmp(section, SECTION_BINDS) == 0) {
		if (tokenCount < 3) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid binding format (needs at least 3 "
				 "tokens)");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: line %d: %s\n",
					lineNum, errMsg);
			}

			freeTokens(tokens, tokenCount);
			return 0;
		}

		const char *modStr  = NULL;
		const char *keyStr  = NULL;
		const char *funcStr = NULL;
		const char *argStr  = NULL;

		modStr	= tokens[0];
		keyStr	= tokens[1];
		funcStr = tokens[2];
		if (tokenCount > 3) {
			argStr = tokens[3];
		}

		if (handleBindsSection(ctx, modStr, keyStr, funcStr, argStr,
				       lineNum, tokens, tokenCount)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else if (strcasecmp(section, SECTION_RULES) == 0) {
		if (tokenCount < 3) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid rule format (needs at least 3 "
				 "tokens)");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: line %d: %s\n",
					lineNum, errMsg);
			}

			freeTokens(tokens, tokenCount);
			return 0;
		}

		if (handleRulesSection(ctx, tokenCount, tokens, lineNum)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else if (strcasecmp(section, SECTION_MASTER) == 0) {
		const char *var = tokens[0];
		const char *val = tokens[1];

		if (handleMasterSection(ctx, var, val, lineNum, tokens,
					tokenCount)) {
			handlerFreedTokens = 1;
			return 0;
		}
	} else {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Unknown section: %s",
			 section);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}
	}

	if (!handlerFreedTokens) {
		freeTokens(tokens, tokenCount);
	}

	return 0;
}

int finalizeConfigParser(STokenHandlerContext *ctx, SKeyBinding *oldKeys,
			 size_t oldKeysCount, SWindowRule *oldRules,
			 size_t oldRulesCount, int braceDepth, int sectionDepth,
			 SSectionInfo *sectionStack,
			 int	       potentialSectionLineNum,
			 char	      *potentialSectionName)
{
	if (braceDepth > 0) {
		reportBraceMismatch(ctx, sectionDepth, sectionStack);
	}

	if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
		int validSectionLimit = sectionDepth - braceDepth;
		for (int i = 0; i < validSectionLimit; i++) {
			if (sectionStack[i].lastContentLine ==
				sectionStack[i].startLine &&
			    !sectionStack[i].isDuplicate &&
			    !sectionStack[i].isInvalid) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Empty section: %s",
					 sectionStack[i].sectionName);
				addError(ctx->errors, errMsg,
					 sectionStack[i].startLine, 0);
				ctx->hasErrors = 1;
			}
		}
	}

	if (potentialSectionLineNum > 0 &&
	    ctx->mode == TOKEN_HANDLER_VALIDATE) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Missing opening brace after section name '%s'",
			 potentialSectionName);
		addError(ctx->errors, errMsg, potentialSectionLineNum, 0);
		ctx->hasErrors = 1;
	}

	if (ctx->mode == TOKEN_HANDLER_LOAD) {
		fprintf(stderr,
			"banana: loaded %zu key bindings and %zu window "
			"rules\n",
			keysCount, rulesCount);
		return 1;
	} else {
		cleanupConfigData();

		restoreConfigState(oldKeys, oldKeysCount, oldRules,
				   oldRulesCount);
		return !ctx->hasErrors;
	}
}

int loadConfig(void)
{
	STokenHandlerContext ctx = {
	    .mode = TOKEN_HANDLER_LOAD, .errors = NULL, .hasErrors = 0};

	ctx.seenSections.count = 0;

	return parseConfigFile(&ctx);
}

int validateConfig(SConfigErrors *errors)
{
	if (!errors) {
		errors = malloc(sizeof(SConfigErrors));
		if (!errors) {
			return 0;
		}
		memset(errors, 0, sizeof(SConfigErrors));
	} else {
		errors->count = 0;
	}

	STokenHandlerContext ctx = {
	    .mode = TOKEN_HANDLER_VALIDATE, .errors = errors, .hasErrors = 0};

	ctx.seenSections.count = 0;

	return parseConfigFile(&ctx);
}

void initDefaults(void)
{
	barFont		     = safeStrdup("monospace 12");
	activeBorderColor    = safeStrdup("#6275d3");
	inactiveBorderColor  = safeStrdup("#6275d3");
	barBorderColor	     = safeStrdup("#000000");
	barBackgroundColor   = safeStrdup("#000000");
	barForegroundColor   = safeStrdup("#ced4f0");
	barActiveWsColor     = safeStrdup("#6275d3");
	barUrgentWsColor     = safeStrdup("#6275d3");
	barActiveTextColor   = safeStrdup("#000000");
	barUrgentTextColor   = safeStrdup("#000000");
	barInactiveTextColor = safeStrdup("#ced4f0");
	barStatusTextColor   = safeStrdup("#ced4f0");
	defaultLayout	     = safeStrdup("master");
}

void *safeMalloc(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr) {
		fprintf(stderr, "banana: failed to allocate memory\n");
		exit(1);
	}
	return ptr;
}

char *safeStrdup(const char *s)
{
	if (!s) {
		return NULL;
	}
	char *result = strdup(s);
	if (!result) {
		fprintf(stderr, "banana: failed to allocate memory for "
				"string\n");
		exit(1);
	}
	return result;
}

char *getConfigPath(void)
{
	const char *home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "banana: HOME environment variable not set\n");
		return NULL;
	}

	char *path = safeMalloc(strlen(home) + strlen(CONFIG_PATH) + 1);
	sprintf(path, "%s%s", home, CONFIG_PATH);
	return path;
}

void trim(char *str)
{
	if (!str) {
		return;
	}

	char *start = str;
	while (isspace((unsigned char)*start)) {
		start++;
	}

	if (start != str) {
		memmove(str, start, strlen(start) + 1);
	}

	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		end--;
	}

	*(end + 1) = '\0';
}

KeySym getKeysym(const char *key)
{
	if (!key) {
		return NoSymbol;
	}

	if (strlen(key) == 1) {
		if (key[0] >= 'a' && key[0] <= 'z') {
			return XK_a + (key[0] - 'a');
		}
		if (key[0] >= '0' && key[0] <= '9') {
			return XK_0 + (key[0] - '0');
		}

		return XStringToKeysym(key);
	}

	if (strcasecmp(key, "escape") == 0) {
		return XK_Escape;
	}
	if (strcasecmp(key, "return") == 0) {
		return XK_Return;
	}
	if (strcasecmp(key, "enter") == 0) {
		return XK_Return;
	}
	if (strcasecmp(key, "tab") == 0) {
		return XK_Tab;
	}
	if (strcasecmp(key, "space") == 0) {
		return XK_space;
	}
	if (strcasecmp(key, "backspace") == 0) {
		return XK_BackSpace;
	}

	if (strcasecmp(key, "f1") == 0) {
		return XK_F1;
	}
	if (strcasecmp(key, "f2") == 0) {
		return XK_F2;
	}
	if (strcasecmp(key, "f3") == 0) {
		return XK_F3;
	}
	if (strcasecmp(key, "f4") == 0) {
		return XK_F4;
	}
	if (strcasecmp(key, "f5") == 0) {
		return XK_F5;
	}
	if (strcasecmp(key, "f6") == 0) {
		return XK_F6;
	}
	if (strcasecmp(key, "f7") == 0) {
		return XK_F7;
	}
	if (strcasecmp(key, "f8") == 0) {
		return XK_F8;
	}
	if (strcasecmp(key, "f9") == 0) {
		return XK_F9;
	}
	if (strcasecmp(key, "f10") == 0) {
		return XK_F10;
	}
	if (strcasecmp(key, "f11") == 0) {
		return XK_F11;
	}
	if (strcasecmp(key, "f12") == 0) {
		return XK_F12;
	}

	if (strcasecmp(key, "up") == 0) {
		return XK_Up;
	}
	if (strcasecmp(key, "down") == 0) {
		return XK_Down;
	}
	if (strcasecmp(key, "left") == 0) {
		return XK_Left;
	}
	if (strcasecmp(key, "right") == 0) {
		return XK_Right;
	}

	if (strcasecmp(key, "shift") == 0 || strcasecmp(key, "control") == 0 ||
	    strcasecmp(key, "alt") == 0 || strcasecmp(key, "super") == 0) {
		return NoSymbol;
	}

	return XStringToKeysym(key);
}

unsigned int getModifier(const char *mod)
{
	if (!mod) {
		return 0;
	}

	unsigned int result  = 0;
	char	    *modCopy = safeStrdup(mod);
	if (!modCopy) {
		return 0;
	}

	char *saveptr = NULL;
	char *token   = strtok_r(modCopy, "+", &saveptr);

	while (token) {
		int found = 0;
		for (int i = 0; modifierMap[i].name; i++) {
			if (strcasecmp(token, modifierMap[i].name) == 0) {
				result |= modifierMap[i].mask;
				found = 1;
				break;
			}
		}

		if (!found) {
			free(modCopy);
			return 0;
		}

		token = strtok_r(NULL, "+", &saveptr);
	}

	free(modCopy);
	return result;
}

void (*getFunction(const char *name))(const char *)
{
	if (!name) {
		return NULL;
	}

	for (int i = 0; functionMap[i].name; i++) {
		if (strcasecmp(name, functionMap[i].name) == 0) {
			return functionMap[i].func;
		}
	}

	return NULL;
}

void freeTokens(char **tokens, int count)
{
	if (!tokens) {
		return;
	}

	for (int i = 0; i < count; i++) {
		if (tokens[i]) {
			free(tokens[i]);
		}
	}

	free(tokens);
}

void createDefaultConfig(void)
{
	char *configPath = getConfigPath();
	if (!configPath) {
		return;
	}

	char *dirPath	= safeStrdup(configPath);
	char *lastSlash = strrchr(dirPath, '/');
	if (lastSlash) {
		*lastSlash = '\0';

		struct stat st;
		if (stat(dirPath, &st) == -1) {
			if (mkdir(dirPath, 0755) == -1) {
				fprintf(stderr,
					"banana: failed to create config "
					"directory: %s\n",
					strerror(errno));
				free(dirPath);
				free(configPath);
				return;
			}
		}
	}
	free(dirPath);

	FILE *fp = fopen(configPath, "w");
	if (!fp) {
		fprintf(stderr, "banana: failed to create config file: %s\n",
			strerror(errno));
		free(configPath);
		return;
	}

	fprintf(fp, "# Variables\n");
	fprintf(fp, "mod \"alt\"\n");
	fprintf(fp, "mod2 \"super\"\n");
	fprintf(fp, "terminal \"alacritty\"\n");
	fprintf(fp, "menu \"dmenu_run\"\n");
	fprintf(fp, "screenshot \"maim -s | xclip -selection "
		    "clipboard -t image/png\"\n\n");

	fprintf(fp, "# Autostart programs\n");
	fprintf(fp, "# exec \"picom\"\n");
	fprintf(fp, "# exec \"dunst\"\n\n");

	fprintf(fp, "# General settings\n");
	fprintf(fp, "general {\n");
	fprintf(fp, "    workspace_count 9\n");
	fprintf(fp, "    inner_gap 0\n");
	fprintf(fp, "    outer_gap 0\n");
	fprintf(fp, "    smart_gaps false\n");
	fprintf(fp, "    border_width 1\n");
	fprintf(fp, "    layout master\n");
	fprintf(fp, "    no_warps false\n");
	fprintf(fp, "}\n\n");

	fprintf(fp, "# Bar settings\n");
	fprintf(fp, "bar {\n");
	fprintf(fp, "    height 20\n");
	fprintf(fp, "    font \"monospace 12\"\n");
	fprintf(fp, "    show true\n");
	fprintf(fp, "    bottom_bar false\n");
	fprintf(fp, "    show_only_active_workspaces false\n");
	fprintf(fp, "    border_width 0\n");
	fprintf(fp, "    struts_top 0\n");
	fprintf(fp, "    struts_left 0\n");
	fprintf(fp, "    struts_right 0\n");
	fprintf(fp, "}\n\n");

	fprintf(fp, "# Decoration\n");
	fprintf(fp, "decoration {\n");
	fprintf(fp, "    active_border_color \"#6275d3\"\n");
	fprintf(fp, "    inactive_border_color \"#595959\"\n");
	fprintf(fp, "    bar_border_color \"#000000\"\n");
	fprintf(fp, "    bar_background_color \"#000000\"\n");
	fprintf(fp, "    bar_foreground_color \"#ced4f0\"\n");
	fprintf(fp, "    bar_active_ws_color \"#6275d3\"\n");
	fprintf(fp, "    bar_urgent_ws_color \"#6275d3\"\n");
	fprintf(fp, "    bar_active_text_color \"#000000\"\n");
	fprintf(fp, "    bar_urgent_text_color \"#000000\"\n");
	fprintf(fp, "    bar_inactive_text_color \"#ced4f0\"\n");
	fprintf(fp, "    bar_status_text_color \"#ced4f0\"\n");
	fprintf(fp, "}\n\n");

	fprintf(fp, "# Master layout\n");
	fprintf(fp, "master {\n");
	fprintf(fp, "    new_as_master false\n");
	fprintf(fp, "    centered_master false\n");
	fprintf(fp, "    factor 0.55\n");
	fprintf(fp, "}\n\n");

	fprintf(fp, "# Key bindings\n");
	fprintf(fp, "binds {\n");
	fprintf(fp, "    $mod q spawn \"$terminal\"\n");
	fprintf(fp, "    $mod e spawn \"$menu\"\n");
	fprintf(fp, "    $mod a spawn \"pocky\"\n");
	fprintf(fp, "    $mod escape spawn \"$screenshot\"\n");
	fprintf(fp, "    $mod c kill\n");
	fprintf(fp, "    $mod w quit\n");
	fprintf(fp, "    $mod space toggle_floating\n");
	fprintf(fp, "    $mod f toggle_fullscreen\n");
	fprintf(fp, "    $mod b toggle_bar\n");
	fprintf(fp, "    $mod r reload_config\n");
	fprintf(fp, "    $mod t cycle_layouts\n\n");

	fprintf(fp, "    $mod h adjust_master \"decrease\"\n");
	fprintf(fp, "    $mod l adjust_master \"increase\"\n\n");

	fprintf(fp, "    $mod+shift j move_window \"down\"\n");
	fprintf(fp, "    $mod+shift k move_window \"up\"\n\n");

	fprintf(fp, "    # Only applies to floating windows\n");
	fprintf(fp, "    $mod+shift h move_window \"left\"\n");
	fprintf(fp, "    $mod+shift l move_window \"right\"\n\n");

	fprintf(fp, "    # Resize floating windows\n");
	fprintf(fp, "    $mod+control h resize_window \"left\"\n");
	fprintf(fp, "    $mod+control j resize_window \"down\"\n");
	fprintf(fp, "    $mod+control k resize_window \"up\"\n");
	fprintf(fp, "    $mod+control l resize_window \"right\"\n\n");

	fprintf(fp, "    # Grow floating windows from the edge\n");
	fprintf(fp, "    $mod+control+shift h resize_window \"grow_left\"\n");
	fprintf(fp, "    $mod+control+shift j resize_window \"grow_down\"\n");
	fprintf(fp, "    $mod+control+shift k resize_window \"grow_up\"\n");
	fprintf(fp, "    $mod+control+shift l resize_window "
		    "\"grow_right\"\n\n");

	fprintf(fp, "    # Focus window in stack, or cycle between monocle "
		    "windows\n");
	fprintf(fp, "    $mod j focus_window \"down\"\n");
	fprintf(fp, "    $mod k focus_window \"up\"\n\n");

	fprintf(fp, "    # Cycle focus between floating and a monocle "
		    "window\n");
	fprintf(fp, "    $mod2 j cycle_focus \"down\"\n");
	fprintf(fp, "    $mod2 k cycle_focus \"up\"\n\n");

	fprintf(fp, "    $mod comma focus_monitor \"left\"\n");
	fprintf(fp, "    $mod period focus_monitor \"right\"\n\n");

	fprintf(fp, "    $mod 1 switch_workspace \"0\"\n");
	fprintf(fp, "    $mod 2 switch_workspace \"1\"\n");
	fprintf(fp, "    $mod 3 switch_workspace \"2\"\n");
	fprintf(fp, "    $mod 4 switch_workspace \"3\"\n");
	fprintf(fp, "    $mod 5 switch_workspace \"4\"\n");
	fprintf(fp, "    $mod 6 switch_workspace \"5\"\n");
	fprintf(fp, "    $mod 7 switch_workspace \"6\"\n");
	fprintf(fp, "    $mod 8 switch_workspace \"7\"\n");
	fprintf(fp, "    $mod 9 switch_workspace \"8\"\n\n");

	fprintf(fp, "    $mod+shift 1 move_to_workspace \"0\"\n");
	fprintf(fp, "    $mod+shift 2 move_to_workspace \"1\"\n");
	fprintf(fp, "    $mod+shift 3 move_to_workspace \"2\"\n");
	fprintf(fp, "    $mod+shift 4 move_to_workspace \"3\"\n");
	fprintf(fp, "    $mod+shift 5 move_to_workspace \"4\"\n");
	fprintf(fp, "    $mod+shift 6 move_to_workspace \"5\"\n");
	fprintf(fp, "    $mod+shift 7 move_to_workspace \"6\"\n");
	fprintf(fp, "    $mod+shift 8 move_to_workspace \"7\"\n");
	fprintf(fp, "    $mod+shift 9 move_to_workspace \"8\"\n");
	fprintf(fp, "}\n\n");

	fprintf(fp, "# Window rules\n");
	fprintf(fp, "# Format: [CLASS] [INSTANCE] [TITLE] [OPTIONS]\n");
	fprintf(fp, "# CLASS: Window class name or * to match any class\n");
	fprintf(fp, "# INSTANCE: Window instance name or * to match any "
		    "instance\n");
	fprintf(fp, "# TITLE: Window title (substring match) or * to match any "
		    "title\n");
	fprintf(fp, "# OPTIONS:\n");
	fprintf(fp, "#   floating - Make window float (not tiled)\n");
	fprintf(fp, "#   follow - Make window follow tiling layout (opposite "
		    "of floating)\n");
	fprintf(fp, "#   workspace N - Place window on workspace N (0-based "
		    "index)\n");
	fprintf(fp, "#   monitor N - Place window on monitor N (0-based "
		    "index)\n");
	fprintf(fp, "#   size WIDTH HEIGHT - Set window dimensions (only works "
		    "with floating windows)\n");
	fprintf(fp, "#   swallow - Enable window swallowing (parent window "
		    "disappears when child spawns and so on)\n");
	fprintf(fp, "#   noswallow - Prevent window from being swallowed by "
		    "any other window\n");
	fprintf(fp, "rules {\n");
	fprintf(fp, "    Pocky * * floating size 1100 700\n");
	fprintf(fp, "    vesktop * * workspace 0 monitor 1\n");
	fprintf(fp, "    Alacritty * * swallow\n");
	fprintf(fp, "}\n");

	fclose(fp);
	fprintf(stderr, "banana: created default config file at %s\n",
		configPath);
	free(configPath);
}

void reloadConfig(const char *arg)
{
	(void)arg;

	fprintf(stderr, "banana: reloading configuration...\n");

	SKeyBinding *oldKeys	   = keys;
	size_t	     oldKeysCount  = keysCount;
	SWindowRule *oldRules	   = rules;
	size_t	     oldRulesCount = rulesCount;

	SAutostart  *oldAutostarts	= autostarts;
	size_t	     oldAutostartsCount = autostartsCount;

	char	    *oldBarFont		     = barFont;
	char	    *oldActiveBorderColor    = activeBorderColor;
	char	    *oldInactiveBorderColor  = inactiveBorderColor;
	char	    *oldBarBorderColor	     = barBorderColor;
	char	    *oldBarBackgroundColor   = barBackgroundColor;
	char	    *oldBarForegroundColor   = barForegroundColor;
	char	    *oldBarActiveWsColor     = barActiveWsColor;
	char	    *oldBarUrgentWsColor     = barUrgentWsColor;
	char	    *oldBarActiveTextColor   = barActiveTextColor;
	char	    *oldBarUrgentTextColor   = barUrgentTextColor;
	char	    *oldBarInactiveTextColor = barInactiveTextColor;
	char	    *oldBarStatusTextColor   = barStatusTextColor;
	char	    *oldDefaultLayout	     = defaultLayout;

	int	     oldWorkspaceCount		 = workspaceCount;
	float	     oldDefaultMasterFactor	 = defaultMasterFactor;
	int	     oldInnerGap		 = innerGap;
	int	     oldOuterGap		 = outerGap;
	int	     oldSmartGaps		 = smartGaps;
	int	     oldBorderWidth		 = borderWidth;
	int	     oldShowBar			 = showBar;
	int	     oldBottomBar		 = bottomBar;
	int	     oldShowOnlyActiveWorkspaces = showOnlyActiveWorkspaces;
	int	     oldBarHeight		 = barHeight;
	int	     oldBarBorderWidth		 = barBorderWidth;
	int	     oldBarStrutsTop		 = barStrutsTop;
	int	     oldBarStrutsLeft		 = barStrutsLeft;
	int	     oldBarStrutsRight		 = barStrutsRight;
	int	     oldNoWarps			 = no_warps;
	int	     oldNewAsMaster		 = newAsMaster;
	int	     oldCenteredMaster		 = centeredMaster;

	keys		     = NULL;
	keysCount	     = 0;
	rules		     = NULL;
	rulesCount	     = 0;
	autostarts	     = NULL;
	autostartsCount	     = 0;
	barFont		     = NULL;
	activeBorderColor    = NULL;
	inactiveBorderColor  = NULL;
	barBorderColor	     = NULL;
	barBackgroundColor   = NULL;
	barForegroundColor   = NULL;
	barActiveWsColor     = NULL;
	barUrgentWsColor     = NULL;
	barActiveTextColor   = NULL;
	barUrgentTextColor   = NULL;
	barInactiveTextColor = NULL;
	barStatusTextColor   = NULL;
	defaultLayout	     = NULL;

	int result = loadConfig();

	if (!result) {
		fprintf(stderr, "banana: failed to reload configuration, "
				"restoring old configuration\n");

		keys			 = oldKeys;
		keysCount		 = oldKeysCount;
		rules			 = oldRules;
		rulesCount		 = oldRulesCount;
		autostarts		 = oldAutostarts;
		autostartsCount		 = oldAutostartsCount;
		barFont			 = oldBarFont;
		activeBorderColor	 = oldActiveBorderColor;
		inactiveBorderColor	 = oldInactiveBorderColor;
		barBorderColor		 = oldBarBorderColor;
		barBackgroundColor	 = oldBarBackgroundColor;
		barForegroundColor	 = oldBarForegroundColor;
		barActiveWsColor	 = oldBarActiveWsColor;
		barUrgentWsColor	 = oldBarUrgentWsColor;
		barActiveTextColor	 = oldBarActiveTextColor;
		barUrgentTextColor	 = oldBarUrgentTextColor;
		barInactiveTextColor	 = oldBarInactiveTextColor;
		barStatusTextColor	 = oldBarStatusTextColor;
		workspaceCount		 = oldWorkspaceCount;
		defaultMasterFactor	 = oldDefaultMasterFactor;
		innerGap		 = oldInnerGap;
		outerGap		 = oldOuterGap;
		smartGaps		 = oldSmartGaps;
		borderWidth		 = oldBorderWidth;
		showBar			 = oldShowBar;
		bottomBar		 = oldBottomBar;
		showOnlyActiveWorkspaces = oldShowOnlyActiveWorkspaces;
		barHeight		 = oldBarHeight;
		barBorderWidth		 = oldBarBorderWidth;
		barStrutsTop		 = oldBarStrutsTop;
		barStrutsLeft		 = oldBarStrutsLeft;
		barStrutsRight		 = oldBarStrutsRight;
		defaultLayout		 = oldDefaultLayout;
		no_warps		 = oldNoWarps;
		newAsMaster		 = oldNewAsMaster;
		centeredMaster		 = oldCenteredMaster;

		return;
	}

	if (display && root) {
		XUngrabKey(display, AnyKey, AnyModifier, root);
		for (size_t i = 0; i < keysCount; i++) {
			XGrabKey(display,
				 XKeysymToKeycode(display, keys[i].keysym),
				 keys[i].mod, root, True, GrabModeAsync,
				 GrabModeAsync);

			XGrabKey(display,
				 XKeysymToKeycode(display, keys[i].keysym),
				 keys[i].mod | LockMask, root, True,
				 GrabModeAsync, GrabModeAsync);
		}
		XSync(display, False);
	}

	for (size_t i = 0; i < oldAutostartsCount; i++) {
		if (oldAutostarts[i].command) {
			free(oldAutostarts[i].command);
		}
	}
	free(oldAutostarts);

	for (size_t i = 0; i < oldKeysCount; i++) {
		free((void *)oldKeys[i].arg);
	}
	free(oldKeys);

	for (size_t i = 0; i < oldRulesCount; i++) {
		free((void *)oldRules[i].className);
		free((void *)oldRules[i].instanceName);
		free((void *)oldRules[i].title);
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
	free(oldDefaultLayout);

	fprintf(stderr,
		"banana: configuration reloaded with %zu key bindings and %zu "
		"window rules\n",
		keysCount, rulesCount);

	if (display) {
		extern void   updateBorders(void);
		extern void   updateClientPositionsForBar(void);
		extern void   updateClientVisibility(void);
		extern void   createBars(void);
		extern void   updateBars(void);
		extern void   showHideBars(int show);
		extern void   tileAllMonitors(void);
		extern void   resetBarResources(void);
		extern int    xerrorHandler(Display *, XErrorEvent *);

		XErrorHandler oldHandler = XSetErrorHandler(xerrorHandler);

		if (!oldHandler) {
			fprintf(stderr, "banana: warning - no error handler "
					"registered, proceeding with "
					"caution\n");
		}

		XSync(display, False);
		updateBorders();
		XSync(display, False);

		resetBarResources();
		XSync(display, False);

		createBars();
		XSync(display, False);

		extern int hasDocks(void);
		if (hasDocks()) {
			showHideBars(0);
			barVisible = 0;
			fprintf(stderr, "Bar hidden due to dock presence after "
					"config reload\n");
		} else {
			showHideBars(showBar);
		}
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

void freeConfig(void)
{
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
		free((char *)keys[i].arg);
	}
	free(keys);

	for (size_t i = 0; i < rulesCount; i++) {
		free((char *)rules[i].className);
		free((char *)rules[i].instanceName);
		free((char *)rules[i].title);
	}
	free(rules);
}

void printConfigErrors(SConfigErrors *errors)
{
	if (!errors || errors->count == 0) {
		printf("No errors found.\n");
		return;
	}

	char *configPath = getConfigPath();
	if (!configPath) {
		fprintf(stderr, "banana: HOME environment variable not set\n");
		return;
	}

	FILE *fp = fopen(configPath, "r");
	if (!fp) {
		fprintf(stderr, "banana: failed to open config file: %s\n",
			strerror(errno));
		free(configPath);
		return;
	}
	free(configPath);

	int maxLineNeeded = 0;
	for (int i = 0; i < errors->count; i++) {
		if (errors->errors[i].lineNum > maxLineNeeded) {
			maxLineNeeded = errors->errors[i].lineNum;
		}
	}

	char **lineContents = safeMalloc((maxLineNeeded + 1) * sizeof(char *));

	for (int i = 0; i <= maxLineNeeded; i++) {
		lineContents[i] = NULL;
	}

	char buffer[MAX_LINE_LENGTH];
	int  lineNum = 0;

	while (fgets(buffer, sizeof(buffer), fp) && lineNum < maxLineNeeded) {
		lineNum++;

		for (int i = 0; i < errors->count; i++) {
			if (errors->errors[i].lineNum == lineNum) {
				char *lineCopy = safeMalloc(MAX_LINE_LENGTH);
				strncpy(lineCopy, buffer, MAX_LINE_LENGTH - 1);
				lineCopy[MAX_LINE_LENGTH - 1] = '\0';
				trim(lineCopy);
				lineContents[lineNum] = lineCopy;
				break;
			}
		}
	}

	fclose(fp);

	for (int i = 0; i < errors->count; i++) {
		if (errors->errors[i].lineNum > 0) {
			printf("  %serror%s[line %d]: %s\n",
			       errors->errors[i].isFatal ? "\x1b[31m"
							 : "\x1b[33m",
			       "\x1b[0m", errors->errors[i].lineNum,
			       errors->errors[i].message);

			if (lineContents[errors->errors[i].lineNum]) {
				printf("     %s\n",
				       lineContents[errors->errors[i].lineNum]);
			}
		} else {
			printf("  %serror%s: %s\n",
			       errors->errors[i].isFatal ? "\x1b[31m"
							 : "\x1b[33m",
			       "\x1b[0m", errors->errors[i].message);
		}
	}

	for (int i = 0; i <= maxLineNeeded; i++) {
		if (lineContents[i]) {
			free(lineContents[i]);
		}
	}
	free(lineContents);
}

void addError(SConfigErrors *errors, const char *message, int lineNum,
	      int isFatal)
{
	if (!errors || errors->count >= MAX_ERRORS) {
		return;
	}

	SConfigError *error = &errors->errors[errors->count++];
	snprintf(error->message, MAX_LINE_LENGTH, "%s", message);
	error->lineNum = lineNum;
	error->isFatal = isFatal;
}

int handleGeneralSection(STokenHandlerContext *ctx, const char *var,
			 const char *val, int lineNum, char **tokens,
			 int tokenCount)
{
	if (strcmp(var, "workspace_count") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid workspace count: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int count = atoi(val);
		if (count > 9) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Workspace count limited to maximum of 9");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				count = 9;
			}
		}

		if (ctx->mode == TOKEN_HANDLER_LOAD) {
			workspaceCount = count;
		}
	} else if (strcmp(var, "inner_gap") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid inner gap: '%s' - must be an integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int gap = atoi(val);
		if (gap < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Inner gap value must be positive, clamping "
				 "to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				innerGap = 0;
			}
		} else if (gap > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Inner gap value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				innerGap = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			innerGap = gap;
		}
	} else if (strcmp(var, "outer_gap") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid outer gap: '%s' - must be an integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int gap = atoi(val);
		if (gap < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Outer gap value must be positive, clamping "
				 "to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				outerGap = 0;
			}
		} else if (gap > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Outer gap value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				outerGap = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			outerGap = gap;
		}
	} else if (strcmp(var, "smart_gaps") == 0) {
		if (strcasecmp(val, "true") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				smartGaps = 1;
			}
		} else if (strcasecmp(val, "false") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				smartGaps = 0;
			}
		} else if (isValidInteger(val)) {
			int smart = atoi(val);
			if (smart != 0 && smart != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid smart gaps value: '%s' - "
					 "must be true, false, 0, or 1",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
					smartGaps = 0;
				}
			} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
				smartGaps = smart;
			}
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid smart gaps value: '%s' - must be "
				 "true, false, 0, or 1",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}
	} else if (strcmp(var, "border_width") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid border width: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int width = atoi(val);
		if (width < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Border width value must be positive, "
				 "clamping to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				borderWidth = 0;
			}
		} else if (width > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Border width value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				borderWidth = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			borderWidth = width;
		}
	} else if (strcmp(var, "layout") == 0) {
		if (strcasecmp(val, "master") != 0 &&
		    strcasecmp(val, "monocle") != 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid layout value: '%s' - must be "
				 "'master' or 'monocle'",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		if (ctx->mode == TOKEN_HANDLER_LOAD) {
			free(defaultLayout);
			defaultLayout = safeStrdup(val);
		}
	} else if (strcmp(var, "no_warps") == 0) {
		if (strcasecmp(val, "true") == 0) {
			no_warps = 1;
		} else if (strcasecmp(val, "false") == 0) {
			no_warps = 0;
		} else if (isValidInteger(val)) {
			int noWarpsValue = atoi(val);
			if (noWarpsValue != 0 && noWarpsValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid no_warps value: '%s' - must "
					 "be true, false, 0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}
			no_warps = noWarpsValue;
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid no_warps value: '%s' - must be true, "
				 "false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Unknown general setting: %s",
			 var);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}
	}

	return 1;
}

int handleBarSection(STokenHandlerContext *ctx, const char *var,
		     const char *val, int lineNum, char **tokens,
		     int tokenCount)
{
	if (strcmp(var, "height") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid bar height: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int height = atoi(val);
		if (height < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Bar height value must be positive, clamping "
				 "to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barHeight = 0;
			}
		} else if (height > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Bar height value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barHeight = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			barHeight = height;
		}
	} else if (strcmp(var, "font") == 0) {
		if (ctx->mode == TOKEN_HANDLER_LOAD) {
			free(barFont);
			barFont = safeStrdup(val);
		}
	} else if (strcmp(var, "show") == 0) {
		if (strcasecmp(val, "true") == 0) {
			showBar = 1;
		} else if (strcasecmp(val, "false") == 0) {
			showBar = 0;
		} else if (isValidInteger(val)) {
			int showValue = atoi(val);
			if (showValue != 0 && showValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid bar show value: '%s' - must "
					 "be true, false, 0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}
			showBar = showValue;
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid bar show value: '%s' - must be true, "
				 "false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else if (strcmp(var, "show_only_active_workspaces") == 0) {
		if (strcasecmp(val, "true") == 0) {
			showOnlyActiveWorkspaces = 1;
		} else if (strcasecmp(val, "false") == 0) {
			showOnlyActiveWorkspaces = 0;
		} else if (isValidInteger(val)) {
			int showActiveValue = atoi(val);
			if (showActiveValue != 0 && showActiveValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid show_only_active_workspaces "
					 "value: '%s' - must be true, false, "
					 "0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}
			showOnlyActiveWorkspaces = showActiveValue;
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid show_only_active_workspaces value: "
				 "'%s' - must be true, false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else if (strcmp(var, "bottom_bar") == 0) {
		if (strcasecmp(val, "true") == 0) {
			bottomBar = 1;
		} else if (strcasecmp(val, "false") == 0) {
			bottomBar = 0;
		} else if (isValidInteger(val)) {
			int bottomBarValue = atoi(val);
			if (bottomBarValue != 0 && bottomBarValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid bottom_bar value: '%s' - "
					 "must be true, false, 0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}
			bottomBar = bottomBarValue;
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid bottom_bar value: '%s' - must be "
				 "true, false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else if (strcmp(var, "border_width") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid bar border width: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int width = atoi(val);
		if (width < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Bar border width value must be positive, "
				 "clamping to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barBorderWidth = 0;
			}
		} else if (width > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Bar border width value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barBorderWidth = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			barBorderWidth = width;
		}
	} else if (strcmp(var, "struts_top") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid struts_top value: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int strutsTop = atoi(val);
		if (strutsTop < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_top value must be positive, clamping "
				 "to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsTop = 0;
			}
		} else if (strutsTop > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_top value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsTop = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			barStrutsTop = strutsTop;
		}
	} else if (strcmp(var, "struts_left") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid struts_left value: '%s' - must be an "
				 "integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int strutsLeft = atoi(val);
		if (strutsLeft < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_left value must be positive, clamping "
				 "to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsLeft = 0;
			}
		} else if (strutsLeft > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_left value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsLeft = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			barStrutsLeft = strutsLeft;
		}
	} else if (strcmp(var, "struts_right") == 0) {
		if (!isValidInteger(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid struts_right value: '%s' - must be "
				 "an integer",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			freeTokens(tokens, tokenCount);
			return 1;
		}

		int strutsRight = atoi(val);
		if (strutsRight < 0) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_right value must be positive, "
				 "clamping to 0");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsRight = 0;
			}
		} else if (strutsRight > 100) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Struts_right value must be at most 100, "
				 "clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				barStrutsRight = 100;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			barStrutsRight = strutsRight;
		}
	} else {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Unknown bar setting: %s",
			 var);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}
	}

	return 1;
}

int handleDecorationSection(STokenHandlerContext *ctx, const char *var,
			    const char *val, int lineNum, char **tokens,
			    int tokenCount)
{
	if (ctx->mode == TOKEN_HANDLER_LOAD) {
		if (strcmp(var, "active_border_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(activeBorderColor);
			activeBorderColor = safeStrdup(val);
		} else if (strcmp(var, "inactive_border_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(inactiveBorderColor);
			inactiveBorderColor = safeStrdup(val);
		} else if (strcmp(var, "bar_border_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barBorderColor);
			barBorderColor = safeStrdup(val);
		} else if (strcmp(var, "bar_background_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barBackgroundColor);
			barBackgroundColor = safeStrdup(val);
		} else if (strcmp(var, "bar_foreground_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barForegroundColor);
			barForegroundColor = safeStrdup(val);
		} else if (strcmp(var, "bar_active_ws_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barActiveWsColor);
			barActiveWsColor = safeStrdup(val);
		} else if (strcmp(var, "bar_urgent_ws_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barUrgentWsColor);
			barUrgentWsColor = safeStrdup(val);
		} else if (strcmp(var, "bar_active_text_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barActiveTextColor);
			barActiveTextColor = safeStrdup(val);
		} else if (strcmp(var, "bar_urgent_text_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barUrgentTextColor);
			barUrgentTextColor = safeStrdup(val);
		} else if (strcmp(var, "bar_inactive_text_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barInactiveTextColor);
			barInactiveTextColor = safeStrdup(val);
		} else if (strcmp(var, "bar_status_text_color") == 0) {
			if (!isValidHexColor(val)) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid color code: '%s' - must be "
					 "in format #RRGGBB",
					 val);

				if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					fprintf(stderr, "banana: %s\n", errMsg);
				}
				freeTokens(tokens, tokenCount);
				return 0;
			}
			free(barStatusTextColor);
			barStatusTextColor = safeStrdup(val);
		} else {
			fprintf(stderr,
				"banana: unknown decoration setting: %s\n",
				var);
		}
	} else if (!(strcmp(var, "active_border_color") == 0 ||
		     strcmp(var, "inactive_border_color") == 0 ||
		     strcmp(var, "bar_border_color") == 0 ||
		     strcmp(var, "bar_background_color") == 0 ||
		     strcmp(var, "bar_foreground_color") == 0 ||
		     strcmp(var, "bar_active_ws_color") == 0 ||
		     strcmp(var, "bar_urgent_ws_color") == 0 ||
		     strcmp(var, "bar_active_text_color") == 0 ||
		     strcmp(var, "bar_urgent_text_color") == 0 ||
		     strcmp(var, "bar_inactive_text_color") == 0 ||
		     strcmp(var, "bar_status_text_color") == 0)) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Unknown decoration setting: %s", var);
		addError(ctx->errors, errMsg, lineNum, 0);
		ctx->hasErrors = 1;
	} else if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
		if (!isValidHexColor(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid color code: '%s' - must be in format "
				 "#RRGGBB",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		}
	}

	return 1;
}

int handleBindsSection(STokenHandlerContext *ctx, const char *modStr,
		       const char *keyStr, const char *funcStr,
		       const char *argStr, int lineNum, char **tokens,
		       int tokenCount)
{
	KeySym keysym = getKeysym(keyStr);
	if (keysym == NoSymbol) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Invalid key: %s", keyStr);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 1;
	}

	unsigned int mod = getModifier(modStr);
	if (!mod) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Unknown modifier: %s",
			 modStr);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 1;
	}

	void (*func)(const char *) = getFunction(funcStr);
	if (!func) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH, "Unknown function: %s",
			 funcStr);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 1;
	}

	if (argStr) {
		int  argValid = 1;
		char errMsg[MAX_LINE_LENGTH];

		if (strcasecmp(funcStr, "switch_workspace") == 0 ||
		    strcasecmp(funcStr, "move_to_workspace") == 0) {
			if (!isValidInteger(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid workspace index: '%s' - must "
					 "be an integer between 0 and 8",
					 argStr);
				argValid = 0;
			} else {
				int value = atoi(argStr);
				if (value < 0 || value > 8) {
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Invalid workspace index: %d "
						 "- must be between 0 and 8",
						 value);
					argValid = 0;
				}
			}
		} else if (strcasecmp(funcStr, "adjust_master") == 0) {
			if (!isValidAdjustMasterArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid adjust_master argument: '%s' "
					 "- must be 'increase' or 'decrease'",
					 argStr);
				argValid = 0;
			}
		} else if (strcasecmp(funcStr, "move_window") == 0) {
			if (!isValidMoveWindowArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid move_window argument: '%s' - "
					 "must be 'up', 'down', 'left', or "
					 "'right'",
					 argStr);
				argValid = 0;
			}
		} else if (strcasecmp(funcStr, "resize_window") == 0) {
			if (!isValidResizeWindowArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid resize_window argument: '%s' "
					 "- "
					 "must be 'up', 'down', 'left', "
					 "'right', 'grow_up', 'grow_down', "
					 "'grow_left', or 'grow_right'",
					 argStr);
				argValid = 0;
			}
		} else if (strcasecmp(funcStr, "focus_window") == 0) {
			if (!isValidFocusWindowArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid focus_window argument: '%s' "
					 "- must be 'up' or 'down'",
					 argStr);
				argValid = 0;
			}
		} else if (strcasecmp(funcStr, "cycle_focus") == 0) {
			if (!isValidCycleFocusArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid cycle_focus argument: '%s' "
					 "- must be 'up' or 'down'",
					 argStr);
				argValid = 0;
			}
		} else if (strcasecmp(funcStr, "focus_monitor") == 0) {
			if (!isValidFocusMonitorArg(argStr)) {
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid focus_monitor argument: '%s' "
					 "- must be 'left' or 'right'",
					 argStr);
				argValid = 0;
			}
		}

		if (!argValid) {
			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}

			freeTokens(tokens, tokenCount);
			return 1;
		}
	}

	if (keysCount >= MAX_KEYS) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Too many key bindings (max: %d)", MAX_KEYS);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 0;
	}

	if (!keys) {
		keys = safeMalloc(MAX_KEYS * sizeof(SKeyBinding));
	}

	keys[keysCount].mod    = mod;
	keys[keysCount].keysym = keysym;
	keys[keysCount].func   = func;
	keys[keysCount].arg    = argStr ? safeStrdup(argStr) : NULL;

	keysCount++;

	return 1;
}

int handleRulesSection(STokenHandlerContext *ctx, int tokenCount, char **tokens,
		       int lineNum)
{
	if (rulesCount >= MAX_RULES) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Too many window rules (max: %d)", MAX_RULES);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}

		freeTokens(tokens, tokenCount);
		return 0;
	}

	if (!rules) {
		rules = safeMalloc(MAX_RULES * sizeof(SWindowRule));
	}

	const char *className	 = tokens[0];
	const char *instanceName = tokens[1];
	const char *title	 = tokens[2];

	if (ctx->mode == TOKEN_HANDLER_LOAD) {
		rules[rulesCount].className    = safeStrdup(tokens[0]);
		rules[rulesCount].instanceName = (strcmp(tokens[1], "*") != 0)
						     ? safeStrdup(tokens[1])
						     : NULL;
		rules[rulesCount].title	       = (strcmp(tokens[2], "*") != 0)
						     ? safeStrdup(tokens[2])
						     : NULL;
	} else {
		rules[rulesCount].className =
		    strcmp(className, "*") == 0 ? NULL : safeStrdup(className);
		rules[rulesCount].instanceName = strcmp(instanceName, "*") == 0
						     ? NULL
						     : safeStrdup(instanceName);
		rules[rulesCount].title =
		    strcmp(title, "*") == 0 ? NULL : safeStrdup(title);
	}

	rules[rulesCount].isFloating = -1;
	rules[rulesCount].workspace  = -1;
	rules[rulesCount].monitor    = -1;
	rules[rulesCount].width	     = -1;
	rules[rulesCount].height     = -1;
	rules[rulesCount].swallowing = -1;
	rules[rulesCount].noswallow  = -1;

	for (int i = 3; i < tokenCount; i++) {
		if (strcasecmp(tokens[i], "floating") == 0) {
			rules[rulesCount].isFloating = 1;
		} else if (strcasecmp(tokens[i], "follow") == 0) {
			rules[rulesCount].isFloating = 0;
		} else if (strcasecmp(tokens[i], "workspace") == 0 &&
			   i + 1 < tokenCount) {
			if (!isValidInteger(tokens[i + 1])) {
				i++;
				continue;
			}

			int workspace = atoi(tokens[++i]);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				if (workspace < 0 || workspace > 8) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Invalid workspace index: %d "
						 "- must be between 0 and 8",
						 workspace);
					addError(ctx->errors, errMsg, lineNum,
						 0);
					ctx->hasErrors = 1;
				} else {
					rules[rulesCount].workspace = workspace;
				}
			} else {
				if (workspace < 0 || workspace > 8) {
					char errMsg[MAX_LINE_LENGTH];
					snprintf(errMsg, MAX_LINE_LENGTH,
						 "Invalid workspace index: %d "
						 "- must be between 0 and 8",
						 workspace);
					fprintf(stderr, "banana: %s\n", errMsg);
				} else {
					rules[rulesCount].workspace = workspace;
				}
			}
		} else if (strcasecmp(tokens[i], "monitor") == 0 &&
			   i + 1 < tokenCount) {
			if (!isValidInteger(tokens[i + 1])) {
				i++;
				continue;
			}

			rules[rulesCount].monitor = atoi(tokens[++i]);
		} else if (strcasecmp(tokens[i], "size") == 0 &&
			   i + 2 < tokenCount) {
			if (!isValidInteger(tokens[i + 1])) {
				i += 2;
				continue;
			}

			if (!isValidInteger(tokens[i + 2])) {
				i += 2;
				continue;
			}

			rules[rulesCount].width	 = atoi(tokens[++i]);
			rules[rulesCount].height = atoi(tokens[++i]);
		} else if (strcasecmp(tokens[i], "swallow") == 0) {
			rules[rulesCount].swallowing = 1;
		} else if (strcasecmp(tokens[i], "noswallow") == 0) {
			rules[rulesCount].noswallow = 1;
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Unknown rule option: %s", tokens[i]);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
		}
	}

	rulesCount++;

	return 1;
}

int handleMasterSection(STokenHandlerContext *ctx, const char *var,
			const char *val, int lineNum,
			char **tokens __attribute__((unused)), int tokenCount)
{
	if (!var || !val) {
		addError(ctx->errors,
			 "Missing variable or value in master section", lineNum,
			 0);
		ctx->hasErrors = 1;
		return 0;
	}

	if (tokenCount < 2) {
		addError(ctx->errors,
			 "Invalid number of tokens in master section", lineNum,
			 0);
		ctx->hasErrors = 1;
		return 0;
	}

	if (strcmp(var, "new_as_master") == 0) {
		if (strcmp(val, "true") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				newAsMaster = 1;
			}
		} else if (strcmp(val, "false") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				newAsMaster = 0;
			}
		} else if (isValidInteger(val)) {
			int newAsMasterValue = atoi(val);
			if (newAsMasterValue != 0 && newAsMasterValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid value for new_as_master: "
					 "'%s' - must be true, false, 0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}

			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				newAsMaster = newAsMasterValue;
			}
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid value for new_as_master: '%s' - must "
				 "be true, false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else if (strcmp(var, "centered_master") == 0) {
		if (strcmp(val, "true") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				centeredMaster = 1;
			}
		} else if (strcmp(val, "false") == 0) {
			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				centeredMaster = 0;
			}
		} else if (isValidInteger(val)) {
			int centeredMasterValue = atoi(val);
			if (centeredMasterValue != 0 &&
			    centeredMasterValue != 1) {
				char errMsg[MAX_LINE_LENGTH];
				snprintf(errMsg, MAX_LINE_LENGTH,
					 "Invalid value for centered_master: "
					 "'%s' - must be true, false, 0, or 1",
					 val);
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
				return 0;
			}

			if (ctx->mode == TOKEN_HANDLER_LOAD) {
				centeredMaster = centeredMasterValue;
			}
		} else {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid value for centered_master: '%s' - "
				 "must be true, false, 0, or 1",
				 val);
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
			return 0;
		}
	} else if (strcmp(var, "factor") == 0) {
		if (!isValidFloat(val)) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Invalid master factor: '%s' - must be a "
				 "floating point number",
				 val);

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
			}
			return 0;
		}

		float factor = atof(val);
		if (factor < 0.10) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Master factor value must be at least "
				 "0.10, clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				defaultMasterFactor = 0.10;
			}
		} else if (factor > 0.90) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Master factor value must be at most "
				 "0.90, clamping");

			if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
				addError(ctx->errors, errMsg, lineNum, 0);
				ctx->hasErrors = 1;
			} else {
				fprintf(stderr, "banana: %s\n", errMsg);
				defaultMasterFactor = 0.90;
			}
		} else if (ctx->mode == TOKEN_HANDLER_LOAD) {
			defaultMasterFactor = factor;
		}
	} else {
		addError(ctx->errors, "Unknown variable in master section",
			 lineNum, 0);
		ctx->hasErrors = 1;
		return 0;
	}

	return 1;
}

int reportBraceMismatch(STokenHandlerContext *ctx, int sectionDepth,
			SSectionInfo *sectionStack)
{
	if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
		for (int i = sectionDepth - 1; i >= 0; i--) {
			char errMsg[MAX_LINE_LENGTH];
			snprintf(errMsg, MAX_LINE_LENGTH,
				 "Unclosed section: %s (missing closing brace)",
				 sectionStack[i].sectionName);

			addError(ctx->errors, errMsg, sectionStack[i].startLine,
				 0);
			ctx->hasErrors = 1;
		}
	}
	return 1;
}

void cleanupConfigData(void)
{
	for (size_t i = 0; i < keysCount; i++) {
		free((char *)keys[i].arg);
	}
	free(keys);
	keys	  = NULL;
	keysCount = 0;

	for (size_t i = 0; i < rulesCount; i++) {
		free((char *)rules[i].className);
		free((char *)rules[i].instanceName);
		free((char *)rules[i].title);
	}
	free(rules);
	rules	   = NULL;
	rulesCount = 0;

	cleanupVariables();
	cleanupAutostart();
}

int processConfigVariable(const char *name, const char *value, int lineNum,
			  STokenHandlerContext *ctx)
{
	if (!name || !value) {
		return 0;
	}

	if (variablesCount >= MAX_VARIABLES) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Too many variables defined (max: %d)", MAX_VARIABLES);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}
		return 0;
	}

	for (size_t i = 0; i < variablesCount; i++) {
		if (variables[i].name && strcmp(variables[i].name, name) == 0) {
			free(variables[i].value);
			variables[i].value = safeStrdup(value);
			return 1;
		}
	}

	if (!variables) {
		variables = safeMalloc(MAX_VARIABLES * sizeof(SVariable));
		if (!variables) {
			return 0;
		}
		for (size_t i = 0; i < MAX_VARIABLES; i++) {
			variables[i].name  = NULL;
			variables[i].value = NULL;
		}
	}

	variables[variablesCount].name	= safeStrdup(name);
	variables[variablesCount].value = safeStrdup(value);
	variablesCount++;

	return 1;
}

const char *getVariableValue(const char *name)
{
	if (!name || !variables || name[0] != '$') {
		return NULL;
	}

	name++;

	for (size_t i = 0; i < variablesCount; i++) {
		if (variables[i].name && strcmp(variables[i].name, name) == 0) {
			return variables[i].value;
		}
	}

	return NULL;
}

char *substituteVariables(const char *str)
{
	if (!str || !variables || variablesCount == 0) {
		return safeStrdup(str);
	}

	size_t	    outputLen = 0;
	const char *p	      = str;

	while (*p) {
		if (*p == '$') {
			const char *varStart = p;
			p++;

			char   varName[MAX_TOKEN_LENGTH] = {0};
			size_t varNameLen		 = 0;

			while (*p &&
			       (isalnum((unsigned char)*p) || *p == '_') &&
			       varNameLen < MAX_TOKEN_LENGTH - 1) {
				varName[varNameLen++] = *p++;
			}
			varName[varNameLen] = '\0';

			if (varNameLen > 0) {
				const char *value = NULL;
				for (size_t i = 0; i < variablesCount; i++) {
					if (variables[i].name &&
					    strcmp(variables[i].name,
						   varName) == 0) {
						value = variables[i].value;
						break;
					}
				}

				if (value) {
					outputLen += strlen(value);
				} else {
					outputLen += (p - varStart);
				}
			} else {
				outputLen++;
			}
		} else {
			outputLen++;
			p++;
		}
	}

	char *output = safeMalloc(outputLen + 1);
	if (!output) {
		return NULL;
	}

	p	  = str;
	char *out = output;

	while (*p) {
		if (*p == '$') {
			const char *varStart = p;
			p++;

			char   varName[MAX_TOKEN_LENGTH] = {0};
			size_t varNameLen		 = 0;

			while (*p &&
			       (isalnum((unsigned char)*p) || *p == '_') &&
			       varNameLen < MAX_TOKEN_LENGTH - 1) {
				varName[varNameLen++] = *p++;
			}
			varName[varNameLen] = '\0';

			if (varNameLen > 0) {
				const char *value = NULL;
				for (size_t i = 0; i < variablesCount; i++) {
					if (variables[i].name &&
					    strcmp(variables[i].name,
						   varName) == 0) {
						value = variables[i].value;
						break;
					}
				}

				if (value) {
					strcpy(out, value);
					out += strlen(value);
				} else {
					size_t len = p - varStart;
					strncpy(out, varStart, len);
					out += len;
				}
			} else {
				*out++ = '$';
			}
		} else {
			*out++ = *p++;
		}
	}

	*out = '\0';
	return output;
}

void cleanupVariables(void)
{
	if (!variables) {
		return;
	}

	for (size_t i = 0; i < variablesCount; i++) {
		if (variables[i].name) {
			free(variables[i].name);
		}
		if (variables[i].value) {
			free(variables[i].value);
		}
	}

	free(variables);
	variables      = NULL;
	variablesCount = 0;
}

int processExecCommand(const char *command, int lineNum,
		       STokenHandlerContext *ctx)
{
	if (!command) {
		return 0;
	}

	if (autostartsCount >= MAX_AUTOSTARTS) {
		char errMsg[MAX_LINE_LENGTH];
		snprintf(errMsg, MAX_LINE_LENGTH,
			 "Too many autostart commands defined (max: %d)",
			 MAX_AUTOSTARTS);

		if (ctx->mode == TOKEN_HANDLER_VALIDATE) {
			addError(ctx->errors, errMsg, lineNum, 0);
			ctx->hasErrors = 1;
		} else {
			fprintf(stderr, "banana: %s\n", errMsg);
		}
		return 0;
	}

	if (!autostarts) {
		autostarts = safeMalloc(MAX_AUTOSTARTS * sizeof(SAutostart));
		if (!autostarts) {
			return 0;
		}
		for (size_t i = 0; i < MAX_AUTOSTARTS; i++) {
			autostarts[i].command = NULL;
		}
	}

	autostarts[autostartsCount].command = safeStrdup(command);
	autostartsCount++;

	return 1;
}

void runAutostart(void)
{
	if (!autostarts || autostartsCount == 0) {
		return;
	}

	for (size_t i = 0; i < autostartsCount; i++) {
		if (autostarts[i].command) {
			char *cmd = substituteVariables(autostarts[i].command);
			if (cmd) {
				fprintf(stderr,
					"Running autostart command: %s\n", cmd);
				spawnProgram(cmd);
				free(cmd);
			}
		}
	}
}

void cleanupAutostart(void)
{
	if (!autostarts) {
		return;
	}

	for (size_t i = 0; i < autostartsCount; i++) {
		if (autostarts[i].command) {
			free(autostarts[i].command);
		}
	}

	free(autostarts);
	autostarts	= NULL;
	autostartsCount = 0;
}