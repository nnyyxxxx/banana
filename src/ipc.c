#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>

#include "ipc.h"

static int	   serverSocket = -1;
static char	   socketPath[SOCKET_PATH_MAX];

static const char *getSocketPath(void)
{
	static char initialized = 0;

	if (!initialized) {
		const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
		if (runtimeDir && *runtimeDir) {
			snprintf(socketPath, SOCKET_PATH_MAX, "%s/banana.sock",
				 runtimeDir);
		} else {
			const char *home = getenv("HOME");
			if (!home || !*home) {
				struct passwd *pw = getpwuid(getuid());
				if (pw) {
					home = pw->pw_dir;
				}
			}

			if (home && *home) {
				snprintf(socketPath, SOCKET_PATH_MAX,
					 "%s/.banana.sock", home);
			} else {
				strncpy(socketPath, "/tmp/banana.sock",
					SOCKET_PATH_MAX);
			}
		}

		initialized = 1;
	}

	return socketPath;
}

static int setNonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		return -1;
	}
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ipcInitServer(void)
{
	const char *path = getSocketPath();

	unlink(path);

	serverSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		fprintf(stderr, "Failed to create IPC socket: %s\n",
			strerror(errno));
		return -1;
	}

	if (setNonblocking(serverSocket) == -1) {
		fprintf(stderr,
			"Failed to set socket to non-blocking mode: %s\n",
			strerror(errno));
		close(serverSocket);
		serverSocket = -1;
		return -1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		fprintf(stderr, "Failed to bind IPC socket to %s: %s\n", path,
			strerror(errno));
		close(serverSocket);
		serverSocket = -1;
		return -1;
	}

	if (listen(serverSocket, 5) == -1) {
		fprintf(stderr, "Failed to listen on IPC socket: %s\n",
			strerror(errno));
		close(serverSocket);
		serverSocket = -1;
		unlink(path);
		return -1;
	}

	fprintf(stderr, "IPC server initialized at %s\n", path);
	return 0;
}

void ipcCleanup(void)
{
	if (serverSocket != -1) {
		close(serverSocket);
		serverSocket = -1;
		unlink(getSocketPath());
	}
}

static void processIpcCommand(int clientFd, SIPCMessage *msg)
{
	SIPCMessage response;
	response.type	= msg->type;
	response.status = 0;
	memset(response.data, 0, sizeof(response.data));

	switch (msg->type) {
	case IPC_COMMAND_RELOAD:
		fprintf(stderr, "Processing reload command via IPC\n");
		strncpy(response.data, "Config reload requested",
			sizeof(response.data) - 1);
		break;

	default:
		fprintf(stderr, "Unknown IPC command: %d\n", msg->type);
		response.status = 1;
		strncpy(response.data, "Unknown command",
			sizeof(response.data) - 1);
		break;
	}

	if (write(clientFd, &response, sizeof(response)) != sizeof(response)) {
		fprintf(stderr, "Failed to send IPC response: %s\n",
			strerror(errno));
	}
}

int ipcHandleCommands(void)
{
	if (serverSocket == -1) {
		return -1;
	}

	int clientFd = accept(serverSocket, NULL, NULL);
	if (clientFd == -1) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			fprintf(stderr, "IPC accept error: %s\n",
				strerror(errno));
		}
		return 0;
	}

	SIPCMessage msg;
	ssize_t	    bytesRead = read(clientFd, &msg, sizeof(msg));

	if (bytesRead == sizeof(msg)) {
		processIpcCommand(clientFd, &msg);
	} else {
		fprintf(stderr, "Invalid IPC message (size %zd)\n", bytesRead);
	}

	close(clientFd);
	return 1;
}

int ipcSendCommand(EIPCCommandType command, const char *data)
{
	const char *path = getSocketPath();

	int	    clientFd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (clientFd == -1) {
		fprintf(stderr, "Failed to create client socket: %s\n",
			strerror(errno));
		return -1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (connect(clientFd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		fprintf(stderr,
			"Failed to connect to banana IPC socket at %s: %s\n",
			path, strerror(errno));
		close(clientFd);
		return -1;
	}

	SIPCMessage msg;
	msg.type   = command;
	msg.status = 0;
	memset(msg.data, 0, sizeof(msg.data));

	if (data) {
		strncpy(msg.data, data, sizeof(msg.data) - 1);
	}

	if (write(clientFd, &msg, sizeof(msg)) != sizeof(msg)) {
		fprintf(stderr, "Failed to send command to banana: %s\n",
			strerror(errno));
		close(clientFd);
		return -1;
	}

	SIPCMessage response;
	if (read(clientFd, &response, sizeof(response)) != sizeof(response)) {
		fprintf(stderr, "Failed to read response from banana: %s\n",
			strerror(errno));
		close(clientFd);
		return -1;
	}

	close(clientFd);

	if (response.data[0]) {
		fprintf(stderr, "Banana response: %s\n", response.data);
	}

	return response.status;
}