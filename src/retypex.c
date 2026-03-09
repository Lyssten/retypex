#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "config.h"
#include "ipc.h"

/* ------------------------------------------------------------------ setup */

static const char *find_hypr_config(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home) return NULL;
    /* prefer keybindings.conf (HyDE / split config) */
    snprintf(path, sizeof(path), "%s/.config/hypr/keybindings.conf", home);
    if (access(path, F_OK) == 0) return path;
    /* fall back to main config */
    snprintf(path, sizeof(path), "%s/.config/hypr/hyprland.conf", home);
    if (access(path, F_OK) == 0) return path;
    return NULL;
}

static int binds_exist(const char *config_path) {
    FILE *f = fopen(config_path, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, "retypex")) { found = 1; break; }
    fclose(f);
    return found;
}

static void write_binds(const char *config_path,
                        const char *word_mod, const char *word_key,
                        const char *sel_mod,  const char *sel_key) {
    FILE *f = fopen(config_path, "a");
    if (!f) { perror("fopen"); return; }
    fprintf(f, "\n# retypex — keyboard layout switcher\n");
    if (word_mod && word_mod[0])
        fprintf(f, "bind = %s, %s, exec, retypex word\n", word_mod, word_key);
    else
        fprintf(f, "bind = , %s, exec, retypex word\n", word_key);
    if (sel_mod && sel_mod[0])
        fprintf(f, "bind = %s, %s, exec, retypex sel\n", sel_mod, sel_key);
    else
        fprintf(f, "bind = , %s, exec, retypex sel\n", sel_key);
    fclose(f);
    printf("Keybinds written to %s\n", config_path);
}

static void ensure_config_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char dir[512], file[512];
    snprintf(dir,  sizeof(dir),  "%s%s", home, CONFIG_DIR);
    snprintf(file, sizeof(file), "%s%s", home, CONFIG_FILE);
    mkdir(dir, 0755);  /* ignore error if already exists */
    if (access(file, F_OK) != 0) {
        FILE *f = fopen(file, "w");
        if (f) {
            fprintf(f, "# retypex configuration\n");
            fprintf(f, "# keyboard = all\n");
            fclose(f);
        }
    }
}

static void strip_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

static int cmd_setup(void) {
    printf("retypex setup\n");
    printf("=============\n\n");

    const char *hypr_config = find_hypr_config();

    if (!hypr_config) {
        fprintf(stderr, "Hyprland config not found.\n"
                        "Add keybinds manually to your Hyprland config:\n\n"
                        "  bind = , Pause, exec, retypex word\n"
                        "  bind = SHIFT, Pause, exec, retypex sel\n\n");
        ensure_config_dir();
        return 1;
    }

    printf("Hyprland config: %s\n\n", hypr_config);

    if (binds_exist(hypr_config)) {
        printf("retypex keybinds already present — skipping.\n");
        ensure_config_dir();
        goto done;
    }

    printf("Choose hotkey pair:\n");
    printf("  1) Pause       / Shift+Pause         (default)\n");
    printf("  2) Print       / Shift+Print          (PrintScreen key)\n");
    printf("  3) Custom\n\n");
    printf("Choice [1-3, Enter = 1]: ");
    fflush(stdout);

    char line[128] = {0};
    if (!fgets(line, sizeof(line), stdin)) return 1;
    strip_newline(line);
    int choice = (line[0] == '\0') ? 1 : atoi(line);
    if (choice < 1 || choice > 3) choice = 1;

    const char *word_mod = "";
    const char *word_key = "";
    const char *sel_mod  = "SHIFT";
    const char *sel_key  = "";
    static char cword_key[64], csel_key[64], csel_mod[64];

    if (choice == 1) {
        word_key = "Pause";
        sel_key  = "Pause";
    } else if (choice == 2) {
        word_key = "Print";
        sel_key  = "Print";
    } else {
        printf("\nWord hotkey\n");
        printf("  Modifier (SHIFT / ALT / SUPER / or leave empty): ");
        fflush(stdout);
        static char cwmod[64];
        if (!fgets(cwmod, sizeof(cwmod), stdin)) return 1;
        strip_newline(cwmod);
        word_mod = cwmod;

        printf("  Key name (e.g. Pause, Print, F13, code:119): ");
        fflush(stdout);
        if (!fgets(cword_key, sizeof(cword_key), stdin)) return 1;
        strip_newline(cword_key);
        word_key = cword_key;

        printf("\nSelection hotkey\n");
        printf("  Modifier (SHIFT / ALT / SUPER / or leave empty): ");
        fflush(stdout);
        if (!fgets(csel_mod, sizeof(csel_mod), stdin)) return 1;
        strip_newline(csel_mod);
        sel_mod = csel_mod;

        printf("  Key name (e.g. Pause, Print, F13): ");
        fflush(stdout);
        if (!fgets(csel_key, sizeof(csel_key), stdin)) return 1;
        strip_newline(csel_key);
        sel_key = csel_key;
    }

    write_binds(hypr_config, word_mod, word_key, sel_mod, sel_key);
    ensure_config_dir();

done:
    printf("\nSetup complete.\n\n");
    printf("Next steps:\n");
    printf("  1. Permissions (if not done): sudo usermod -aG input $USER  (re-login required)\n");
    printf("  2. udev rule:                 sudo cp /etc/udev/rules.d/99-uinput.rules exists? check install/\n");
    printf("  3. Start daemon:              systemctl --user enable --now retypexd\n");
    printf("  4. Reload Hyprland:           hyprctl reload\n");
    printf("  5. Install wtype (optional):  sudo pacman -S wtype\n");
    return 0;
}

/* ------------------------------------------------------------------- main */

static void usage(void) {
    fprintf(stderr,
            "Usage: retypex <command>\n\n"
            "Commands:\n"
            "  word   Convert last typed word to opposite layout\n"
            "  sel    Convert selected text to opposite layout\n"
            "  setup  Interactive first-run configuration wizard\n"
            "  quit   Stop the daemon\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 1; }

    if (strcmp(argv[1], "setup") == 0) return cmd_setup();

    const char *cmd;
    if      (strcmp(argv[1], "word") == 0) cmd = IPC_CMD_WORD;
    else if (strcmp(argv[1], "sel")  == 0) cmd = IPC_CMD_SEL;
    else if (strcmp(argv[1], "quit") == 0) cmd = IPC_CMD_QUIT;
    else { usage(); return 1; }

    config_load();
    const char *path = config_socket_path();

    int fd = ipc_client_connect(path);
    if (fd < 0) {
        fprintf(stderr, "retypex: cannot connect to daemon at %s\n"
                        "Run 'systemctl --user start retypexd' or './retypexd'\n",
                path);
        return 1;
    }

    ipc_send(fd, cmd);
    close(fd);
    return 0;
}
