#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <linux/input.h>

#include "config.h"
#include "ipc.h"
#include "buffer.h"
#include "evdev.h"
#include "uinput.h"
#include "layout.h"

#define MAX_EVENTS  64
#define TRAIL_MAX  100

static volatile int running = 1;
static int          uinput_fd = -1;

/* physical modifier state */
static bool shift_held = false;
static bool ctrl_held  = false;

/*
 * Word tracking:
 *   word_buf   — keys of the word currently being typed
 *   last_word  — keys of the last completed word (before trailing separators)
 *   trail_*    — separator key codes typed after last_word (spaces, tabs…)
 */
static WordBuffer word_buf;
static WordBuffer last_word;
static uint16_t   trail_codes[TRAIL_MAX];
static int        trail_count = 0;

/*
 * Selection conversion dedup:
 *   sel_from — PRIMARY content we last converted FROM (to skip stale repeats)
 *   sel_to   — PRIMARY content we last converted TO   (to skip if app keeps selection)
 */
static char sel_from[65536] = {0};
static char sel_to[65536]   = {0};

static void sig_handler(int sig) { (void)sig; running = 0; }

/* ----------------------------------------------------------------- helpers */

static void read_cmd(const char *cmd, char *buf, size_t size) {
    buf[0] = '\0';
    FILE *f = popen(cmd, "r");
    if (!f) return;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    pclose(f);
}

static void switch_layout(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "hyprctl switchxkblayout %s next >/dev/null 2>&1",
             g_config.keyboard);
    system(cmd);
}

/*
 * Type text using wtype (Wayland-native, no clipboard/paste-shortcut needed).
 * Returns 0 on success, -1 if wtype is not installed or failed.
 */
static int wtype_text(const char *text) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* child: redirect stdout/stderr to /dev/null */
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); close(null); }
        char *argv[] = { "wtype", "--", (char *)text, NULL };
        execvp("wtype", argv);
        exit(127);  /* wtype not found */
    }
    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/*
 * Paste via clipboard + Ctrl+V fallback (when wtype is unavailable).
 * Saves and restores the original clipboard content.
 */
static void clipboard_paste(const char *text) {
    char saved[65536] = {0};
    read_cmd("wl-paste --no-newline 2>/dev/null", saved, sizeof(saved));

    FILE *w = popen("wl-copy", "w");
    if (w) { fputs(text, w); pclose(w); }

    usleep(30000);
    uinput_emit_ctrl_key(uinput_fd, KEY_V);
    usleep(150000);

    if (saved[0] != '\0') {
        w = popen("wl-copy", "w");
        if (w) { fputs(saved, w); pclose(w); }
    } else {
        system("wl-copy --clear 2>/dev/null");
    }
}

/* -------------------------------------------------------------- conversion */

static void convert_word(void) {
    if (word_buf.len > 0) {
        int len = word_buf.len;
        const KeyEntry *keys = buf_keys(&word_buf);

        uinput_emit_backspace(uinput_fd, len);
        switch_layout();
        usleep(30000);
        for (int i = 0; i < len; i++)
            uinput_emit_key(uinput_fd, keys[i].code, keys[i].shift);

    } else if (last_word.len > 0) {
        /* cursor is after trailing separators: "word   |" */
        int len = last_word.len;
        const KeyEntry *keys = buf_keys(&last_word);

        uinput_emit_backspace(uinput_fd, trail_count + len);
        switch_layout();
        usleep(30000);
        for (int i = 0; i < len; i++)
            uinput_emit_key(uinput_fd, keys[i].code, keys[i].shift);
        for (int i = 0; i < trail_count; i++)
            uinput_emit_key(uinput_fd, trail_codes[i], false);
    }
}

static void convert_selection(void) {
    /*
     * Read PRIMARY selection.
     * On Wayland, highlighted text is placed in PRIMARY automatically.
     * No Ctrl+C needed — avoids all modifier/timing issues.
     */
    char selected[65536] = {0};
    read_cmd("wl-paste --primary --no-newline 2>/dev/null", selected, sizeof(selected));

    /* nothing selected */
    if (selected[0] == '\0') return;

    /*
     * Stale PRIMARY guard:
     * Skip if PRIMARY content matches the last text we converted FROM
     * or the last text we converted TO (app may keep selection after paste).
     */
    if (strcmp(selected, sel_from) == 0 || strcmp(selected, sel_to) == 0)
        return;

    char *converted = layout_convert(selected);
    if (!converted) return;

    /* record for dedup */
    snprintf(sel_from, sizeof(sel_from), "%s", selected);
    snprintf(sel_to,   sizeof(sel_to),   "%s", converted);

    /*
     * Output: prefer wtype (types text directly as Wayland key events,
     * works in all apps including terminals, no paste shortcut needed).
     * Fall back to clipboard + Ctrl+V if wtype is not installed.
     */
    if (wtype_text(converted) != 0)
        clipboard_paste(converted);

    free(converted);
}

/* -------------------------------------------------------------------  IPC */

static void handle_ipc_cmd(int client_fd) {
    char buf[IPC_BUF_SIZE];
    if (ipc_recv(client_fd, buf, sizeof(buf)) < 0) { close(client_fd); return; }

    if      (strcmp(buf, IPC_CMD_WORD) == 0) convert_word();
    else if (strcmp(buf, IPC_CMD_SEL)  == 0) convert_selection();
    else if (strcmp(buf, IPC_CMD_QUIT) == 0) running = 0;

    close(client_fd);
}

/* ------------------------------------------------------------------ evdev */

static void handle_key_event(const struct input_event *ie) {
    uint16_t code  = ie->code;
    int32_t  value = ie->value;

    if (code == KEY_LEFTSHIFT  || code == KEY_RIGHTSHIFT)
        shift_held = (value != 0);
    if (code == KEY_LEFTCTRL   || code == KEY_RIGHTCTRL)
        ctrl_held  = (value != 0);

    if (value != 1 && value != 2) return;

    if (code == KEY_BACKSPACE) {
        if (word_buf.len > 0)
            buf_pop(&word_buf);
        else if (trail_count > 0)
            trail_count--;

    } else if (ctrl_held || code == KEY_DELETE) {
        buf_clear(&word_buf);
        buf_clear(&last_word);
        trail_count = 0;

    } else if (evdev_is_separator(code)) {
        if (word_buf.len > 0) {
            last_word   = word_buf;
            buf_clear(&word_buf);
            trail_count = 0;
        }
        if (trail_count < TRAIL_MAX)
            trail_codes[trail_count++] = code;

    } else if (evdev_is_nav_key(code)) {
        buf_clear(&word_buf);
        buf_clear(&last_word);
        trail_count = 0;

    } else if (evdev_is_char_key(code)) {
        if (trail_count > 0) {
            buf_clear(&last_word);
            trail_count = 0;
        }
        buf_push(&word_buf, code, shift_held);
    }
}

/* ================================================================== main == */

int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    config_load();

    /* first-run hint: if no config file exists yet */
    {
        const char *home = getenv("HOME");
        if (home) {
            char cfg[512];
            snprintf(cfg, sizeof(cfg), "%s%s", home, CONFIG_FILE);
            if (access(cfg, F_OK) != 0)
                fprintf(stderr, "retypexd: first run — no config found. "
                                "Run 'retypex setup' to configure keybinds.\n");
        }
    }

    const char *sock_path = config_socket_path();

    /* single-instance guard */
    {
        int probe = ipc_client_connect(sock_path);
        if (probe >= 0) {
            fprintf(stderr, "retypexd: already running (socket: %s)\n", sock_path);
            close(probe);
            return 1;
        }
    }

    int kbd_fds[MAX_KEYBOARDS];
    int n_kbd = evdev_open_keyboards(kbd_fds, MAX_KEYBOARDS);
    if (n_kbd == 0) {
        fprintf(stderr, "retypexd: no keyboard devices found — "
                        "are you in the 'input' group?\n");
        return 1;
    }
    fprintf(stderr, "retypexd: monitoring %d keyboard(s)\n", n_kbd);

    uinput_fd = uinput_open();
    if (uinput_fd < 0) {
        fprintf(stderr, "retypexd: failed to open uinput — "
                        "check /etc/udev/rules.d/99-uinput.rules\n");
        return 1;
    }

    int ipc_fd = ipc_server_create(sock_path);
    if (ipc_fd < 0) return 1;

    buf_init(&word_buf);
    buf_init(&last_word);

    /* snapshot current PRIMARY so a pre-existing selection is not auto-triggered */
    read_cmd("wl-paste --primary --no-newline 2>/dev/null", sel_from, sizeof(sel_from));

    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    struct epoll_event ev = { .events = EPOLLIN };
    ev.data.fd = ipc_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc_fd, &ev);
    for (int i = 0; i < n_kbd; i++) {
        ev.data.fd = kbd_fds[i];
        epoll_ctl(epfd, EPOLL_CTL_ADD, kbd_fds[i], &ev);
    }

    fprintf(stderr, "retypexd: ready — socket=%s\n", sock_path);

    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == ipc_fd) {
                int client = ipc_server_accept(ipc_fd);
                if (client >= 0) handle_ipc_cmd(client);
                continue;
            }
            struct input_event ie;
            while (read(fd, &ie, sizeof(ie)) == sizeof(ie))
                if (ie.type == EV_KEY) handle_key_event(&ie);
        }
    }

    close(epfd);
    close(ipc_fd);
    unlink(sock_path);
    for (int i = 0; i < n_kbd; i++) close(kbd_fds[i]);
    uinput_close(uinput_fd);
    return 0;
}
