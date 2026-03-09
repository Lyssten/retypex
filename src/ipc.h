#pragma once

#define IPC_CMD_WORD "WORD"
#define IPC_CMD_SEL  "SEL"
#define IPC_CMD_QUIT "QUIT"
#define IPC_BUF_SIZE 64

/* daemon side */
int ipc_server_create(const char *path);
int ipc_server_accept(int server_fd);

/* client side */
int ipc_client_connect(const char *path);

/* shared */
int ipc_send(int fd, const char *cmd);
int ipc_recv(int fd, char *buf, int bufsize);
