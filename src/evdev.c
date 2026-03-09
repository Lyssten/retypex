#include "evdev.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static int test_bit(const unsigned char *bitmask, int bit) {
    return (bitmask[bit / 8] >> (bit % 8)) & 1;
}

static int is_keyboard(int fd) {
    unsigned char evtype[EV_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evtype)), evtype) < 0)
        return 0;
    if (!test_bit(evtype, EV_KEY))
        return 0;

    unsigned char keys[KEY_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
        return 0;

    /* Must have KEY_SPACE and at least some letter keys */
    return test_bit(keys, KEY_SPACE) && test_bit(keys, KEY_A);
}

int evdev_open_keyboards(int *fds, int maxfds) {
    int count = 0;
    struct dirent *de;
    DIR *d = opendir("/dev/input");
    if (!d) { perror("opendir /dev/input"); return 0; }

    while ((de = readdir(d)) && count < maxfds) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;

        char path[280];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (is_keyboard(fd)) {
            fds[count++] = fd;
        } else {
            close(fd);
        }
    }
    closedir(d);
    return count;
}

bool evdev_is_char_key(uint16_t code) {
    switch (code) {
    /* top row */
    case KEY_Q: case KEY_W: case KEY_E: case KEY_R: case KEY_T:
    case KEY_Y: case KEY_U: case KEY_I: case KEY_O: case KEY_P:
    /* middle row */
    case KEY_A: case KEY_S: case KEY_D: case KEY_F: case KEY_G:
    case KEY_H: case KEY_J: case KEY_K: case KEY_L:
    /* bottom row */
    case KEY_Z: case KEY_X: case KEY_C: case KEY_V: case KEY_B:
    case KEY_N: case KEY_M:
    /* symbol keys */
    case KEY_1: case KEY_2: case KEY_3: case KEY_4: case KEY_5:
    case KEY_6: case KEY_7: case KEY_8: case KEY_9: case KEY_0:
    case KEY_MINUS:      case KEY_EQUAL:
    case KEY_LEFTBRACE:  case KEY_RIGHTBRACE:
    case KEY_SEMICOLON:  case KEY_APOSTROPHE: case KEY_GRAVE:
    case KEY_BACKSLASH:  case KEY_COMMA:
    case KEY_DOT:        case KEY_SLASH:
        return true;
    default:
        return false;
    }
}

bool evdev_is_separator(uint16_t code) {
    return code == KEY_SPACE || code == KEY_ENTER ||
           code == KEY_KPENTER || code == KEY_TAB;
}

bool evdev_is_nav_key(uint16_t code) {
    switch (code) {
    case KEY_LEFT: case KEY_RIGHT: case KEY_UP:    case KEY_DOWN:
    case KEY_HOME: case KEY_END:   case KEY_PAGEUP: case KEY_PAGEDOWN:
    case KEY_INSERT:
        return true;
    default:
        return false;
    }
}
