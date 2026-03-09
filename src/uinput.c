#include "uinput.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include <linux/input.h>

static void _emit(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type  = type;
    ie.code  = code;
    ie.value = value;
    if (write(fd, &ie, sizeof(ie)) < 0)
        perror("uinput write");
}

static void syn(int fd) {
    _emit(fd, EV_SYN, SYN_REPORT, 0);
}

int uinput_open(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return -1; }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    /* enable all standard keys */
    for (int i = 0; i < KEY_MAX; i++)
        ioctl(fd, UI_SET_KEYBIT, i);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;
    snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "retypex virtual keyboard");

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        perror("UI_DEV_SETUP"); close(fd); return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE"); close(fd); return -1;
    }

    /* give udev/Hyprland time to pick up the new device */
    usleep(100000);

    return fd;
}

void uinput_close(int fd) {
    if (fd < 0) return;
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

void uinput_emit_key(int fd, uint16_t code, bool shift) {
    if (shift) { _emit(fd, EV_KEY, KEY_LEFTSHIFT, 1); syn(fd); }
    _emit(fd, EV_KEY, code, 1); syn(fd);
    _emit(fd, EV_KEY, code, 0); syn(fd);
    if (shift) { _emit(fd, EV_KEY, KEY_LEFTSHIFT, 0); syn(fd); }
}

void uinput_emit_backspace(int fd, int count) {
    for (int i = 0; i < count; i++) {
        _emit(fd, EV_KEY, KEY_BACKSPACE, 1); syn(fd);
        _emit(fd, EV_KEY, KEY_BACKSPACE, 0); syn(fd);
        usleep(3000);  /* 3ms — let compositor process each backspace */
    }
}

void uinput_emit_ctrl_key(int fd, uint16_t code) {
    _emit(fd, EV_KEY, KEY_LEFTCTRL, 1); syn(fd);
    _emit(fd, EV_KEY, code, 1);         syn(fd);
    _emit(fd, EV_KEY, code, 0);         syn(fd);
    _emit(fd, EV_KEY, KEY_LEFTCTRL, 0); syn(fd);
}
