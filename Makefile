CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O3 -Isrc
LDFLAGS ?= -lxcb -lxcb-util -lxcb-keysyms -lxcb-icccm -lxcb-xinerama -lxcb-xkb -lxcb-randr -lxcb-cursor -lxcb-xrm `pkg-config --libs cairo pango pangocairo glib-2.0 fontconfig freetype2` -lm
FT_CFLAGS = $(shell pkg-config --cflags freetype2)
CAIRO_CFLAGS = $(shell pkg-config --cflags cairo pango pangocairo)

PREFIX  ?= /usr/local
BIN     := build/banana
SRC_DIR := src
OBJ_DIR := build
SRC     := $(wildcard $(SRC_DIR)/*.c)
OBJ     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))
LOGO    := .github/banana.svg

all: clean release

format:
	clang-format -i src/*.c src/*.h

release: CFLAGS += -O3
release: format $(BIN)

debug: CFLAGS += -g
debug: format $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(FT_CFLAGS) $(CAIRO_CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN:build/%=%)
	mkdir -p $(DESTDIR)$(PREFIX)/share/pixmaps
	install -m 644 $(LOGO) $(DESTDIR)$(PREFIX)/share/pixmaps/banana.svg

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN:build/%=%)
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/banana.svg

.PHONY: all clean release debug install uninstall format