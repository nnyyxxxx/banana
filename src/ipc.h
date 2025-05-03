#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define SOCKET_PATH_MAX 108

typedef enum {
	IPC_COMMAND_RELOAD   = 1,
	IPC_COMMAND_VALIDATE = 2
} EIPCCommandType;

typedef struct {
	EIPCCommandType type;
	int		status;
	char		data[256];
} SIPCMessage;

int  ipcInitServer(void);

void ipcCleanup(void);

int  ipcHandleCommands(void);

int  ipcSendCommand(EIPCCommandType command, const char *data);

#endif /* IPC_H */