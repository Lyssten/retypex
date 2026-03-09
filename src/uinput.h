#pragma once

#include <stdint.h>
#include <stdbool.h>

int  uinput_open(void);
void uinput_close(int fd);

/* Press and release a key, optionally wrapping with shift */
void uinput_emit_key(int fd, uint16_t code, bool shift);

/* Emit count backspace presses */
void uinput_emit_backspace(int fd, int count);

/* Emit Ctrl+key combo */
void uinput_emit_ctrl_key(int fd, uint16_t code);
