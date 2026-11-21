# Usage:
#   make                      plain build
#   make MITSHM=1             use the MIT-SHM extension for the screenshot
#   make LIVE=1               live-update the screenshot every frame
#   make SELECT=1             let the user click a window to track
#   make DEVELOPER=1          load shaders from src/*.glsl at runtime (Ctrl+R reloads them)
#   make install PREFIX=~/.local

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra
PREFIX  ?= /usr/local

PKGS     = x11 xrandr gl
ifdef MITSHM
PKGS    += xext
CPPFLAGS += -DMITSHM
endif
ifdef LIVE
CPPFLAGS += -DLIVE
endif
ifdef SELECT
CPPFLAGS += -DSELECT
endif
ifdef DEVELOPER
CPPFLAGS += -DDEVELOPER
endif

VERSION := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-8)
ifneq ($(VERSION),)
CPPFLAGS += -DBOOMER_VERSION='"$(VERSION)"'
endif

CPPFLAGS += -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm

SRCS = src/boomer.c src/config.c src/navigation.c src/screenshot.c
OBJS = $(SRCS:src/%.c=build/%.o)

all: boomer

boomer: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/%.o: src/%.c $(wildcard src/*.h) build/shaders.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

# Embed the GLSL sources as C string literals.
build/shaders.h: src/vert.glsl src/frag.glsl | build
	{ \
	  printf 'static const char vert_glsl[] =\n'; \
	  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' src/vert.glsl; \
	  printf ';\n'; \
	  printf 'static const char frag_glsl[] =\n'; \
	  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' src/frag.glsl; \
	  printf ';\n'; \
	} > $@

install: boomer
	install -D -m 755 boomer $(DESTDIR)$(PREFIX)/bin/boomer

clean:
	rm -rf build boomer

.PHONY: all install clean
