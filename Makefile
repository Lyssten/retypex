CC      = gcc
VERSION = 1.0.0
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE -DVERSION=\"$(VERSION)\" -Isrc
PREFIX  = /usr/local

DAEMON_SRCS = src/daemon.c src/evdev.c src/uinput.c \
              src/buffer.c src/layout.c src/config.c src/ipc.c
CLI_SRCS    = src/retypex.c src/config.c src/ipc.c

.PHONY: all install uninstall clean

all: retypexd retypex

retypexd: $(DAEMON_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

retypex: $(CLI_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	install -Dm755 retypexd  $(DESTDIR)$(PREFIX)/bin/retypexd
	install -Dm755 retypex   $(DESTDIR)$(PREFIX)/bin/retypex
	install -Dm644 install/retypexd.service \
	    $(DESTDIR)/usr/lib/systemd/user/retypexd.service
	install -Dm644 install/99-uinput.rules \
	    $(DESTDIR)/etc/udev/rules.d/99-uinput.rules

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/retypexd
	rm -f $(DESTDIR)$(PREFIX)/bin/retypex
	rm -f $(DESTDIR)/usr/lib/systemd/user/retypexd.service
	rm -f $(DESTDIR)/etc/udev/rules.d/99-uinput.rules

clean:
	rm -f retypexd retypex
