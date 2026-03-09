#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
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

static void sig_handler(int sig) { (void)sig; running = 0; }

/* ----------------------------------------------------------------- layout */

static void switch_layout(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "hyprctl switchxkblayout %s next >/dev/null 2>&1",
             g_config.keyboard);
    system(cmd);
}

/* -------------------------------------------------------------- conversion */

static void convert_word(void) {
    if (word_buf.len > 0) {
        /* cursor is inside or at end of current word */
        int len = word_buf.len;
        const KeyEntry *keys = buf_keys(&word_buf);

        uinput_emit_backspace(uinput_fd, len);
        switch_layout();
        usleep(30000);
        for (int i = 0; i < len; i++)
            uinput_emit_key(uinput_fd, keys[i].code, keys[i].shift);

    } else if (last_word.len > 0) {
        /* cursor is after trailing separators (e.g. typed "word  |") */
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
    /* buffer intentionally unchanged — hotkey again toggles back */
}

static void convert_selection(void) {
    /* save current clipboard so we can restore it */
    char saved[65536] = {0};
    FILE *f = popen("wl-paste --no-newline 2>/dev/null", "r");
    if (f) { fread(saved, 1, sizeof(saved) - 1, f); pclose(f); }

    /*
     * Read selected text from the PRIMARY selection.
     * On Wayland, highlighted text lands in PRIMARY automatically — no
     * Ctrl+C needed, so no timing/modifier issues.
     */
    char selected[65536] = {0};
    f = popen("wl-paste --primary --no-newline 2>/dev/null", "r");
    if (!f) return;
    fread(selected, 1, sizeof(selected) - 1, f);
    pclose(f);

    if (selected[0] == '\0') return;

    char *converted = layout_convert(selected);
    if (!converted) return;

    /* put converted text on the clipboard */
    FILE *w = popen("wl-copy", "w");
    if (w) { fputs(converted, w); pclose(w); }
    free(converted);

    /* small pause for the clipboard to be set */
    usleep(20000);

    /* paste — virtual keyboard has no held modifiers, so Ctrl+V is clean */
    uinput_emit_ctrl_key(uinput_fd, KEY_V);

    /* restore original clipboard after paste completes */
    usleep(150000);
    if (saved[0] != '\0') {
        w = popen("wl-copy", "w");
        if (w) { fputs(saved, w); pclose(w); }
    } else {
        system("wl-copy --clear 2>/dev/null");
    }
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
        /* ctrl combos or Delete break word context entirely */
        buf_clear(&word_buf);
        buf_clear(&last_word);
        trail_count = 0;

    } else if (evdev_is_separator(code)) {
        /*
         * Separator after a word: archive word_buf → last_word, reset trail.
         * Separator after separators: just keep accumulating trail.
         */
        if (word_buf.len > 0) {
            last_word   = word_buf;  /* struct copy */
            buf_clear(&word_buf);
            trail_count = 0;
        }
        if (trail_count < TRAIL_MAX)
            trail_codes[trail_count++] = code;

    } else if (evdev_is_nav_key(code)) {
        /* cursor moved — buffer no longer represents screen state */
        buf_clear(&word_buf);
        buf_clear(&last_word);
        trail_count = 0;

    } else if (evdev_is_char_key(code)) {
        if (trail_count > 0) {
            /* starting a brand-new word; previous last_word is unreachable */
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
    const char *sock_path = config_socket_path();

    /* single-instance guard: try connecting to an existing socket */
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
