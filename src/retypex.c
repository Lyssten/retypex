#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "ipc.h"

static void usage(void) {
    fprintf(stderr, "Usage: retypex <word|sel|quit>\n"
                    "  word  - convert last typed word\n"
                    "  sel   - convert selected text\n"
                    "  quit  - stop the daemon\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 1; }

    const char *arg = argv[1];
    const char *cmd;

    if      (strcmp(arg, "word") == 0) cmd = IPC_CMD_WORD;
    else if (strcmp(arg, "sel")  == 0) cmd = IPC_CMD_SEL;
    else if (strcmp(arg, "quit") == 0) cmd = IPC_CMD_QUIT;
    else { usage(); return 1; }

    config_load();
    const char *path = config_socket_path();

    int fd = ipc_client_connect(path);
    if (fd < 0) {
        fprintf(stderr, "retypex: cannot connect to daemon at %s\n"
                        "Is retypexd running?\n", path);
        return 1;
    }

    ipc_send(fd, cmd);
    close(fd);
    return 0;
}
