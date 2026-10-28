CC ?= cc
PKGS = gtk4 libadwaita-1 vte-2.91-gtk4
CFLAGS += -std=gnu11 -O2 -g -Wall -Wextra -Wformat=2 -Wno-deprecated-declarations
LDLIBS = $(shell pkg-config --libs $(PKGS))
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))

.PHONY: all clean install uninstall
all: build/slate-terminal

build/slate-terminal: src/main.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDLIBS)

install: all
	install -Dm755 build/slate-terminal "$(HOME)/.local/bin/slate-terminal"
	install -Dm644 assets/dev.slate.Terminal.desktop "$(HOME)/.local/share/applications/dev.slate.Terminal.desktop"
	install -Dm644 assets/dev.slate.Terminal.png "$(HOME)/.local/share/icons/hicolor/512x512/apps/dev.slate.Terminal.png"

uninstall:
	rm -f "$(HOME)/.local/bin/slate-terminal" "$(HOME)/.local/share/applications/dev.slate.Terminal.desktop" "$(HOME)/.local/share/icons/hicolor/512x512/apps/dev.slate.Terminal.png"

clean:
	rm -rf build
