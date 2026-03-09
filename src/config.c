#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config g_config = {
    .keyboard = "all",
};

static void trim(char *s) {
    /* trailing whitespace */
    int n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

void config_load(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, CONFIG_FILE);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) != 2) continue;
        trim(key);
        trim(val);
        if (strcmp(key, "keyboard") == 0)
            snprintf(g_config.keyboard, sizeof(g_config.keyboard), "%s", val);
    }
    fclose(f);
}

const char *config_socket_path(void) {
    static char path[256];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime) runtime = "/tmp";
    snprintf(path, sizeof(path), "%s/retypex.sock", runtime);
    return path;
}
