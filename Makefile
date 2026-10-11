# Teglon - solver and screensaver for the tiling problem in Neal Stephenson's "Anathem"
VERSION  := 1.0.0
PREFIX   ?= /usr
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wno-trigraphs
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/teglon build/teglon-solve

build/teglon-solve: src/cli.c src/teglon.c src/teglon.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/cli.c src/teglon.c $(LDFLAGS) -lm

build/teglon: src/saver.c src/teglon.c src/teglon.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/saver.c src/teglon.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/teglon-solve
	@for n in 1 2 3 4 5 6 8 12 16 24; do ./build/teglon-solve -n $$n -s $$n -q > /dev/null || exit 1; done; echo "all sizes solved and verified"
	@./build/teglon-solve -n 2 -g 01,12,01,xx,25,xx,05 -q > /dev/null && echo "strict side 2 solved and verified"

install: all
	install -Dm755 build/teglon $(DESTDIR)$(PREFIX)/bin/teglon
	install -Dm755 build/teglon-solve $(DESTDIR)$(PREFIX)/bin/teglon-solve
	install -Dm755 packaging/linux/teglon-screensaver $(DESTDIR)$(PREFIX)/bin/teglon-screensaver
	install -Dm644 packaging/linux/teglon.desktop $(DESTDIR)$(PREFIX)/share/applications/teglon.desktop
	install -Dm644 packaging/teglon.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/teglon.svg
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/teglon/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/teglon/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/teglon $(DESTDIR)$(PREFIX)/bin/teglon-solve $(DESTDIR)$(PREFIX)/bin/teglon-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/teglon.desktop $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/teglon.svg
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/teglon

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/teglon build/deb/usr/bin/teglon-solve
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	dpkg-deb --root-owner-group --build build/deb dist/teglon_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=teglon-$(VERSION)/ -o build/rpm/SOURCES/teglon-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/teglon.spec
	cp build/rpm/RPMS/*/teglon-$(VERSION)-*.rpm dist/

windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Wno-trigraphs -o build/win/teglon-solve.exe src/cli.c src/teglon.c -lm -static
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Wno-trigraphs -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/Teglon.scr src/saver.c src/teglon.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-strip build/win/Teglon.scr build/win/teglon-solve.exe
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/teglon.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
