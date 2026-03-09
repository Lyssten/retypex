# retypex

Switch keyboard layout of the last typed word (or selected text) in any window on Wayland/Hyprland.

## How it works

`retypexd` monitors keyboard input via evdev, maintains a buffer of the current word's key codes. On trigger:
- **Word mode**: emits backspaces × word_len, switches layout via `hyprctl`, re-emits the same key codes
- **Selection mode**: copies selection via clipboard, converts text, pastes back, restores clipboard

## TODO

- [x] Project setup and architecture
- [x] `src/config.c/h` — config file parsing
- [x] `src/ipc.c/h` — Unix socket IPC (daemon ↔ CLI)
- [x] `src/buffer.c/h` — word key-code buffer
- [x] `src/layout.c/h` — RU↔EN Unicode text conversion
- [x] `src/evdev.c/h` — keyboard device discovery and monitoring
- [x] `src/uinput.c/h` — virtual keyboard (uinput)
- [x] `src/daemon.c` — main daemon (epoll event loop)
- [x] `src/retypex.c` — CLI tool (sends IPC commands)
- [x] Makefile + build verification (0 warnings)
- [x] `install/retypexd.service` — systemd user service
- [x] `install/99-uinput.rules` — udev rules for /dev/uinput access
- [ ] Test: terminal (kitty/foot), browser (Firefox), GUI apps
- [ ] Test: mid-word cursor, mixed layouts, long words
- [ ] Selected text conversion test
- [ ] AUR PKGBUILD

## Dependencies

- Linux kernel headers (`linux/input.h`, `linux/uinput.h`)
- `wl-clipboard` (`wl-paste`, `wl-copy`) — for selection mode
- `wtype` — **recommended** for selection mode output (types text natively on Wayland, no paste shortcut needed); falls back to `wl-copy` + Ctrl+V if absent
- `hyprland` with `hyprctl` in PATH

## Setup

### 1. Permissions

```bash
sudo usermod -aG input $USER
# For uinput — either add rule (preferred) or add to group:
sudo cp install/99-uinput.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Re-login for group changes to take effect
```

### 2. Build and install

```bash
make
sudo make install
```

### 3. Start daemon

```bash
systemctl --user enable --now retypexd
```

### 4. Hyprland config

Add to `~/.config/hypr/hyprland.conf`:

```
bind = , Pause, exec, retypex word
bind = SHIFT, Pause, exec, retypex sel
```

## Config

`~/.config/retypex/config`:

```
# Keyboard device name for hyprctl switchxkblayout (default: all)
keyboard = all
```

## Architecture

```
Real keyboard ──► /dev/input/eventX ──► retypexd (evdev monitor, no grab)
                                           │  word buffer (key codes)
                                           │
Hyprland bind ──► retypex word/sel ──────► retypexd (Unix socket IPC)
                                           │
                                           ▼
                                    /dev/uinput ──► virtual keyboard ──► Hyprland ──► focused app
                                           │
                                    hyprctl switchxkblayout all next
```
