#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_KEYBOARDS 32

/* Open all keyboard input devices; returns count of opened fds */
int  evdev_open_keyboards(int *fds, int maxfds);

/* True if evdev key code is a printable character key */
bool evdev_is_char_key(uint16_t code);

/* True if key code is a word separator (space/enter/tab) */
bool evdev_is_separator(uint16_t code);

/* True if key code is a cursor navigation key (arrow keys, home, end...) */
bool evdev_is_nav_key(uint16_t code);
