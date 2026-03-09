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

#define MAX_EVENTS 64

static volatile int running = 1;
static int          uinput_fd = -1;
static WordBuffer   word_buf;
static bool         shift_held = false;
static bool         ctrl_held  = false;

static void sig_handler(int sig) { (void)sig; running = 0; }

/* ------------------------------------------------------------------ helpers */

static void switch_layout(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "hyprctl switchxkblayout %s next >/dev/null 2>&1",
             g_config.keyboard);
    system(cmd);
}

static void convert_word(void) {
    int len = buf_len(&word_buf);
    if (len == 0) return;

    const KeyEntry *keys = buf_keys(&word_buf);

    /* 1. delete the word */
    uinput_emit_backspace(uinput_fd, len);

    /* 2. switch layout */
    switch_layout();

    /* 3. wait for compositor to process layout change */
    usleep(30000);

    /* 4. re-emit same key codes — new layout interprets them differently */
    for (int i = 0; i < len; i++)
        uinput_emit_key(uinput_fd, keys[i].code, keys[i].shift);

    /* buffer unchanged: same key codes, pressing hotkey again toggles back */
}

static void convert_selection(void) {
    /* save current clipboard */
    char saved[65536] = {0};
    FILE *f = popen("wl-paste --no-newline 2>/dev/null", "r");
    if (f) { fread(saved, 1, sizeof(saved) - 1, f); pclose(f); }

    /* copy selection */
    uinput_emit_ctrl_key(uinput_fd, KEY_C);
    usleep(100000);

    /* read selection from clipboard */
    char selected[65536] = {0};
    f = popen("wl-paste --no-newline 2>/dev/null", "r");
    if (!f) return;
    fread(selected, 1, sizeof(selected) - 1, f);
    pclose(f);

    if (selected[0] == '\0') return;

    /* convert */
    char *converted = layout_convert(selected);
    if (!converted) return;

    /* write converted to clipboard */
    FILE *w = popen("wl-copy", "w");
    if (w) { fputs(converted, w); pclose(w); }
    free(converted);

    /* paste */
    uinput_emit_ctrl_key(uinput_fd, KEY_V);
    usleep(100000);

    /* restore original clipboard */
    w = popen("wl-copy", "w");
    if (w) { fputs(saved, w); pclose(w); }
}

/* ----------------------------------------------------------------- ipc */

static void handle_ipc_cmd(int client_fd) {
    char buf[IPC_BUF_SIZE];
    if (ipc_recv(client_fd, buf, sizeof(buf)) < 0) return;

    if (strcmp(buf, IPC_CMD_WORD) == 0) {
        convert_word();
    } else if (strcmp(buf, IPC_CMD_SEL) == 0) {
        convert_selection();
    } else if (strcmp(buf, IPC_CMD_QUIT) == 0) {
        running = 0;
    }

    close(client_fd);
}

/* ----------------------------------------------------------------- evdev */

static void handle_key_event(const struct input_event *ie) {
    uint16_t code  = ie->code;
    int32_t  value = ie->value; /* 0=up, 1=down, 2=repeat */

    /* track modifier state */
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT)
        shift_held = (value != 0);
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL)
        ctrl_held = (value != 0);

    /* only care about press/repeat for buffer updates */
    if (value != 1 && value != 2) return;

    if (code == KEY_BACKSPACE) {
        buf_pop(&word_buf);
    } else if (evdev_is_separator(code) || ctrl_held) {
        buf_clear(&word_buf);
    } else if (evdev_is_nav_key(code)) {
        /* cursor moved — buffer out of sync */
        buf_clear(&word_buf);
    } else if (evdev_is_char_key(code)) {
        buf_push(&word_buf, code, shift_held);
    }
}

/* ================================================================= main == */

int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    config_load();

    /* open keyboards */
    int kbd_fds[MAX_KEYBOARDS];
    int n_kbd = evdev_open_keyboards(kbd_fds, MAX_KEYBOARDS);
    if (n_kbd == 0) {
        fprintf(stderr, "retypexd: no keyboard devices found. "
                        "Are you in the 'input' group?\n");
        return 1;
    }
    fprintf(stderr, "retypexd: monitoring %d keyboard(s)\n", n_kbd);

    /* create virtual output keyboard */
    uinput_fd = uinput_open();
    if (uinput_fd < 0) {
        fprintf(stderr, "retypexd: failed to open uinput. "
                        "Check /etc/udev/rules.d/99-uinput.rules\n");
        return 1;
    }

    /* IPC server */
    const char *sock_path = config_socket_path();
    int ipc_fd = ipc_server_create(sock_path);
    if (ipc_fd < 0) return 1;

    buf_init(&word_buf);

    /* epoll */
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    struct epoll_event ev;
    ev.events = EPOLLIN;

    ev.data.fd = ipc_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc_fd, &ev);

    for (int i = 0; i < n_kbd; i++) {
        ev.data.fd = kbd_fds[i];
        epoll_ctl(epfd, EPOLL_CTL_ADD, kbd_fds[i], &ev);
    }

    fprintf(stderr, "retypexd: ready. socket=%s\n", sock_path);

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

            /* keyboard event */
            struct input_event ie;
            while (read(fd, &ie, sizeof(ie)) == sizeof(ie)) {
                if (ie.type == EV_KEY)
                    handle_key_event(&ie);
            }
        }
    }

    /* cleanup */
    close(epfd);
    close(ipc_fd);
    unlink(sock_path);
    for (int i = 0; i < n_kbd; i++) close(kbd_fds[i]);
    uinput_close(uinput_fd);

    return 0;
}
