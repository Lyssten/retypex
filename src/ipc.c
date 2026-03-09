#include "ipc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

int ipc_server_create(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    unlink(path);  /* remove stale socket */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 8) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

int ipc_server_accept(int server_fd) {
    int fd = accept(server_fd, NULL, NULL);
    if (fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        perror("accept");
    return fd;
}

int ipc_client_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return -1;
    }
    return fd;
}

int ipc_send(int fd, const char *cmd) {
    char buf[IPC_BUF_SIZE];
    snprintf(buf, sizeof(buf), "%s\n", cmd);
    ssize_t n = write(fd, buf, strlen(buf));
    return (n > 0) ? 0 : -1;
}

int ipc_recv(int fd, char *buf, int bufsize) {
    ssize_t n = read(fd, buf, bufsize - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    /* strip trailing newline */
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
    return 0;
}
