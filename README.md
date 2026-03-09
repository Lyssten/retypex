# retypex

Switch keyboard layout of the last typed word (or selected text) in any window on Wayland/Hyprland — without leaving your current application.

## Features

- **Word mode**: re-types the current word in the opposite layout (RU↔EN) with a single hotkey
- **Selection mode**: converts any highlighted text to the opposite layout
- Works in any Wayland application — browsers, terminals, GUI apps, editors
- Non-intrusive: reads keyboard events passively (no grab), injects output via uinput
- Daemon architecture keeps latency minimal; CLI sends instant IPC commands

## Requirements

- Hyprland (with `hyprctl` in PATH)
- `wl-clipboard` (`wl-paste`, `wl-copy`) — required for selection mode
- `wtype` — **optional but recommended** for selection mode; types text natively as Wayland key events (works in terminals too); falls back to `wl-copy` + Ctrl+V if absent
- `gcc` (build only)
- Linux kernel headers (`linux/input.h`, `linux/uinput.h`) — present on any standard Linux system

## Installation

### From AUR

```bash
yay -S retypex-git
# or
paru -S retypex-git
```

### Manual build

```bash
git clone https://github.com/Lyssten/retypex
cd retypex
make
sudo make install
```

## First-run setup

Fast path (recommended, one command):

```bash
retypex quickstart
```

`quickstart` will:
1. Add default Hyprland binds (`Print` and `Shift+Print`) if missing
2. Ensure `/etc/udev/rules.d/99-uinput.rules` exists
3. Reload udev and fix `/dev/uinput` permissions for current boot
4. Add your user to `input` group if needed
5. Enable and start `retypexd`
6. Reload Hyprland config (if binds were added)

Interactive wizard is still available:

```bash
retypex setup
```

The wizard will:
1. Detect your Hyprland config file (`hyprland.conf` or `keybindings.conf`)
2. Let you choose hotkeys (Pause, PrintScreen, or custom)
3. Append the `bind =` lines to your Hyprland config
4. Create `~/.config/retypex/config` if it does not exist

Manual steps (alternative to `quickstart`):

```bash
# Add yourself to the input group (re-login required)
sudo usermod -aG input $USER

# Install udev rule for /dev/uinput access
sudo cp install/99-uinput.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# Enable and start the daemon
systemctl --user enable --now retypexd

# Reload Hyprland to pick up new keybinds
hyprctl reload
```

## Default hotkeys

| Action               | Default hotkey  |
|----------------------|-----------------|
| Convert last word    | Print (quickstart) / Pause (setup) |
| Convert selection    | Shift + Print (quickstart) / Shift + Pause (setup) |

Hotkeys are configured in your Hyprland config via `retypex setup` or manually.

## Config

`~/.config/retypex/config`:

```ini
# Keyboard device name passed to hyprctl switchxkblayout (default: all)
keyboard = all
```

Set `keyboard` to a specific device name (as shown by `hyprctl devices`) if you have multiple keyboards and only want to switch one.

## How it works

`retypexd` monitors raw keyboard events from `/dev/input/eventX` using evdev (no exclusive grab — other apps receive keystrokes normally). It maintains a rolling buffer of the current word's key codes.

On trigger (via IPC from the `retypex` CLI):

- **Word mode**: emits backspaces equal to the word length, calls `hyprctl switchxkblayout <keyboard> next`, then re-emits the same key codes — producing the word in the other layout.
- **Selection mode**: reads the Wayland PRIMARY selection (`wl-paste --primary`), converts the text character-by-character between RU and EN layouts, then outputs the result via `wtype` (or falls back to clipboard + Ctrl+V).

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

## Maintainers

Maintenance workflow for GitHub + AUR is documented in [docs/MAINTENANCE.md](docs/MAINTENANCE.md).

## Troubleshooting

**retypexd: no keyboard devices found**
You are not in the `input` group. Run `sudo usermod -aG input $USER` and re-login.

**retypexd: failed to open uinput**
The udev rule for `/dev/uinput` is not installed. Copy `install/99-uinput.rules` to `/etc/udev/rules.d/` and reload udev rules (see installation steps above).

**Selection mode does not work in terminals**
Install `wtype` (`sudo pacman -S wtype`). Without it, selection mode falls back to Ctrl+V paste, which does not work in terminals that use Ctrl+V for other purposes.

**Nothing happens when I press the hotkey**
- Check that the daemon is running: `systemctl --user status retypexd`
- Check that Hyprland config has the binds: `grep retypex ~/.config/hypr/*.conf`
- Reload Hyprland: `hyprctl reload`

## TODO

- [ ] Test: terminal (kitty/foot), browser (Firefox), GUI apps
- [ ] Test: mid-word cursor, mixed layouts, long words
- [ ] Selected text conversion end-to-end test
- [x] AUR PKGBUILD

## License

MIT — see [LICENSE](LICENSE).
