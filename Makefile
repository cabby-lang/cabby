# Build for cabby, the Cabby compiler and interpreter.
#   make            build ./cabby (and the lexdump/astdump development tools)
#   make test       run every test suite
#   make debug      build with AddressSanitizer + UBSan into build-debug/
#   make install    install to $(PREFIX) (default /usr/local)

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g
CFLAGS  += -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
DBGFLAGS = -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD   := build
CORE    := arena.c diag.c util.c lexer.c ast.c ast_print.c parser.c types.c style.c \
           sema_decl.c sema_expr.c sema_stmt.c sema_flow.c interpreter.c codegen.c repl.c lineedit.c highlight.c runtime/rt.c runtime/dates.c runtime/json.c runtime/csv.c runtime/networks.c runtime/markup.c runtime/bytes.c runtime/patterns.c runtime/hashes.c runtime/archives.c runtime/databases.c runtime/threads.c runtime/dbus.c runtime/bluetooth.c runtime/emails.c runtime/secrets.c runtime/desktop.c runtime/zones.c runtime/connections.c runtime/images.c runtime/os.c runtime/linux.c
LDLIBS  := -lffi -lm -ldl -pthread

# macOS: Homebrew's libffi (it isn't on the usual paths), the BSD and
# Darwin parts of the C library, and libraries that are allowed to use the
# interpreter's own functions (as Linux allows by itself).
UNAME := $(shell uname -s)
# Homebrew for this Mac's own processor: on an Apple one it's in
# /opt/homebrew, and a Homebrew in /usr/local is Intel's (for Rosetta),
# whose libraries can't be linked here
ARCH := $(shell uname -m)
BREW := brew
PKG_CONFIG := pkg-config
ifeq ($(UNAME)-$(ARCH),Darwin-arm64)
BREW := $(firstword $(wildcard /opt/homebrew/bin/brew) brew)
PKG_CONFIG := $(firstword $(wildcard /opt/homebrew/bin/pkg-config) pkg-config)
endif
# $(call LIB_FOR_HERE,library file,what to give): what to give, if the
# library is built for this processor (a Mac's lipo says; elsewhere, yes)
LIB_FOR_HERE = $(if $(filter Darwin,$(UNAME)),$(if $(filter $(ARCH),$(shell lipo -archs $(1) 2>/dev/null)),$(2)),$(2))
ifeq ($(UNAME),Darwin)
PLATFORM_DEFS := -D_DARWIN_C_SOURCE
PLATFORM_SHARED := -Wl,-undefined,dynamic_lookup
# the first thread's stack (where a window program runs): 60 MB, under
# macOS's usual limit of 64
LDFLAGS += -Wl,-stack_size,0x3c00000
SOUND_FRAMEWORKS := -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
CFLAGS += $(PLATFORM_DEFS)
# ffi.h: Homebrew's libffi (wherever Homebrew is: /opt/homebrew or
# /usr/local), or else the one in Apple's own SDK; only one built for this
# processor (lipo says), so an Intel Homebrew's isn't taken on an Apple one
FFI_FOR_HERE = $(if $(filter $(ARCH),$(shell lipo -archs $(dir $(1))../lib/libffi.dylib 2>/dev/null)),$(1))
BREW_FFI_ALL := $(wildcard $(shell $(BREW) --prefix libffi 2>/dev/null)/include/ffi.h) \
	$(wildcard /opt/homebrew/opt/libffi/include/ffi.h) $(wildcard /usr/local/opt/libffi/include/ffi.h)
BREW_FFI := $(firstword $(foreach f,$(BREW_FFI_ALL),$(call FFI_FOR_HERE,$(f))))
ifneq ($(BREW_FFI_ALL),)
ifeq ($(BREW_FFI),)
$(warning Homebrew's libffi isn't built for $(ARCH) (it's in $(dir $(firstword $(BREW_FFI_ALL)))): Apple's own is used instead; /opt/homebrew/bin/brew install libffi gives one)
endif
endif
SDK_FFI := $(wildcard $(shell xcrun --show-sdk-path 2>/dev/null)/usr/include/ffi/ffi.h)
ifneq ($(BREW_FFI),)
CFLAGS += -I$(dir $(BREW_FFI))
LDFLAGS += -L$(dir $(BREW_FFI))../lib
else ifneq ($(SDK_FFI),)
CFLAGS += -I$(dir $(SDK_FFI))
else
$(warning ffi.h wasn't found: install libffi with 'brew install libffi')
endif
endif
CORE_O  := $(CORE:%.c=$(BUILD)/%.o)


.PHONY: wasm wasm-site
.PHONY: install-vim install-extras install-icons uninstall-icons uninstall-vim uninstall all debug test clean install gui gtk game world videos

# The gtk library (`use gtk`, the gui library drawn by GTK, an option)
# needs GTK 4: found with pkg-config, or (without GTK's development
# headers) by the libraries' names, using runtime/gtk_min.h.
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GUI_WARN := -Wall -Wextra -Wno-unused-parameter -Werror=incompatible-pointer-types -Werror=int-conversion -Werror=implicit-function-declaration
ifeq ($(strip $(GTK_LIBS)),)
GTK_LIBS := $(shell for l in libgtk-4.so.1 libgio-2.0.so.0 libgobject-2.0.so.0 libglib-2.0.so.0 libcairo.so.2 libpango-1.0.so.0; do \
	ldconfig -p 2>/dev/null | awk -v l="$$l" '$$1 == l { print $$NF; exit }'; done | tr '\n' ' ')
ifneq ($(words $(GTK_LIBS)),6)
GTK_LIBS :=
endif
endif
ifneq ($(strip $(GTK_LIBS)),)
GTK_TARGETS := lib/libcabbygtk.so lib/libcabbygtk.a lib/gtk.link
endif

TUI_TARGETS := lib/libcabbytui.so lib/libcabbytui.a lib/tui.link $(PROC_TARGETS) $(SOUND_TARGETS)
PROC_TARGETS := lib/libcabbyprocesses.so lib/libcabbyprocesses.a lib/processes.link
SOUND_TARGETS := lib/libcabbysound.so lib/libcabbysound.a lib/sound.link

# The game library needs SDL 3 (found with pkg-config): sdl3 on Arch,
# libsdl3-dev on Debian.
GAME_LIBS := $(shell $(PKG_CONFIG) --libs sdl3 2>/dev/null)
GAME_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 2>/dev/null)
ifeq ($(strip $(GAME_LIBS)),)
# (without pkg-config: where Homebrew puts it, or the usual places)
SDL_FOUND := $(patsubst %/include/SDL3/SDL.h,%,$(wildcard $(shell $(BREW) --prefix sdl3 2>/dev/null)/include/SDL3/SDL.h \
	/opt/homebrew/opt/sdl3/include/SDL3/SDL.h /usr/local/opt/sdl3/include/SDL3/SDL.h /usr/local/include/SDL3/SDL.h))
SDL_PREFIX := $(firstword $(foreach p,$(SDL_FOUND),$(call LIB_FOR_HERE,$(p)/lib/libSDL3.dylib,$(p))))
ifneq ($(SDL_PREFIX),)
GAME_LIBS := -L$(SDL_PREFIX)/lib -lSDL3
GAME_CFLAGS := -I$(SDL_PREFIX)/include
endif
endif
ifneq ($(strip $(GAME_LIBS)),)
GAME_TARGETS := lib/libcabbygame.so lib/libcabbygame.a lib/game.link
TGAME_TARGETS := lib/libcabbytgame.so lib/libcabbytgame.a lib/tgame.link
GUI_TARGETS := lib/libcabbygui.so lib/libcabbygui.a lib/gui.link
WORLD_TARGETS := lib/libcabbyworld.so lib/libcabbyworld.a lib/world.link
endif

all: cabby lib/libcabbyrt.a $(BUILD)/lexdump $(BUILD)/astdump gui gtk game world videos $(TUI_TARGETS) $(PROC_TARGETS) $(SOUND_TARGETS)

# the gui library (`use gui`): windows, drawn by Cabby itself with SDL 3. A
# shared library for the interpreter, a static one to link into compiled
# programs, and the flags those programs need
gui: $(GUI_TARGETS)
ifeq ($(strip $(GAME_LIBS)),)
	@echo "note: SDL 3 was not found, so the gui and game libraries were not built (install sdl3 on Arch, libsdl3-dev on Debian, or 'brew install sdl3' on a Mac)"
endif

GUI_SRC := runtime/gui.c runtime/video_source.h runtime/accessibility.h version.h runtime/sdl_common.h runtime/text_layout.h runtime/colors.h runtime/game_font.h runtime/third_party/stb_image.h runtime/third_party/stb_image_write.h runtime/third_party/stb_truetype.h
lib/libcabbygui.so: $(GUI_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) runtime/gui.c -o $@ $(GAME_LIBS) -lm -ldl

lib/libcabbygui.a: $(GUI_SRC) | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) -c runtime/gui.c -o $(BUILD)/gui_lib.o
	ar rcs $@ $(BUILD)/gui_lib.o

lib/gui.link: Makefile
	echo "$(GAME_LIBS) -ldl" > $@

# the gtk library (`use gtk`): the same objects and methods, drawn by GTK 4.
# Its header is made from gui's, with gtk. names (always, so `use gtk` can
# say what's missing when GTK isn't installed)
gtk: lib/gtk.hby $(GTK_TARGETS)

lib/gtk.hby: lib/gui.hby
	sed 's/gui\./gtk./g; s/cby_gui_/cby_gtk_/g; s/use gui$$/use gtk/; s/^\/\/ gtk.hby —/\/\/ gtk.hby (made from gui.hby by the Makefile) —/' lib/gui.hby > $@

lib/libcabbygtk.so: runtime/gtk.c runtime/gtk_min.h
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GTK_CFLAGS) runtime/gtk.c -o $@ $(GTK_LIBS)

lib/libcabbygtk.a: runtime/gtk.c runtime/gtk_min.h | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GTK_CFLAGS) -c runtime/gtk.c -o $(BUILD)/gtk_lib.o
	ar rcs $@ $(BUILD)/gtk_lib.o

lib/gtk.link: Makefile
	echo "$(GTK_LIBS)" > $@

# the tui library: windows in a terminal; it needs nothing but the C library
lib/libcabbytui.so: runtime/tui.c
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) runtime/tui.c -o $@

lib/libcabbytui.a: runtime/tui.c | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/tui.c -o $(BUILD)/tui_lib.o
	ar rcs $@ $(BUILD)/tui_lib.o

lib/tui.link: Makefile
	echo "" > $@

game: $(GAME_TARGETS) lib/tgame.hby $(TGAME_TARGETS)

# the game library: a window with a stage, sprites and frames, on SDL 3
# (stb_image, stb_image_write and stb_truetype are built into it)
GAME_SRC := runtime/game.c runtime/sdl_common.h runtime/text_layout.h runtime/colors.h runtime/game_font.h runtime/third_party/stb_image.h runtime/third_party/stb_image_write.h runtime/third_party/stb_truetype.h
lib/libcabbygame.so: $(GAME_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) runtime/game.c -o $@ $(GAME_LIBS) -lm

lib/libcabbygame.a: $(GAME_SRC) | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) -c runtime/game.c -o $(BUILD)/game_lib.o
	ar rcs $@ $(BUILD)/game_lib.o

lib/game.link: Makefile
	echo "$(GAME_LIBS)" > $@

# the tgame library (`use tgame`): the game library again, shown in a
# terminal. game.c is built with TERMINAL_GAME (tgame_terminal.h) and
# cby_tgame_ names; its header is made from game's, with tgame. names
# (always, so `use tgame` can say what's missing without SDL 3)
$(BUILD)/tgame.c: runtime/game.c | $(BUILD)
	sed 's/cby_game_/cby_tgame_/g' runtime/game.c > $@

TGAME_SRC := $(BUILD)/tgame.c $(GAME_SRC) runtime/tgame_terminal.h
lib/libcabbytgame.so: $(TGAME_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -DTERMINAL_GAME -Iruntime $(GAME_CFLAGS) $(BUILD)/tgame.c -o $@ $(GAME_LIBS) -lm -ldl

lib/libcabbytgame.a: $(TGAME_SRC)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -DTERMINAL_GAME -Iruntime $(GAME_CFLAGS) -c $(BUILD)/tgame.c -o $(BUILD)/tgame_lib.o
	ar rcs $@ $(BUILD)/tgame_lib.o

lib/tgame.link: Makefile
	echo "$(GAME_LIBS) -ldl" > $@

lib/tgame.hby: lib/game.hby
	sed 's/\([^a-z_]\)game\./\1tgame./g; s/^game\./tgame./; s/cby_game_/cby_tgame_/g; s/use game$$/use tgame/; s/^\/\/ tgame.hby —/\/\/ tgame.hby (made from game.hby by the Makefile) — in a terminal:/' lib/game.hby > $@

# the world library (`use world`): 3D, on SDL 3 and OpenGL (looked up when
# a window opens: nothing more to link). Built without fused multiply-adds,
# so Cabby's own drawing (for tests) is the same on every computer.
world: $(WORLD_TARGETS)

WORLD_SRC := runtime/world.c runtime/world_soft.h runtime/world_gl.h runtime/world_text.h runtime/world_models.h runtime/third_party/cgltf.h runtime/sdl_common.h runtime/text_layout.h runtime/colors.h runtime/game_font.h runtime/third_party/stb_image.h runtime/third_party/stb_image_write.h runtime/third_party/stb_truetype.h
lib/libcabbyworld.so: $(WORLD_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared -ffp-contract=off $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) runtime/world.c -o $@ $(GAME_LIBS) -lm -ldl

lib/libcabbyworld.a: $(WORLD_SRC) | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC -ffp-contract=off $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(GAME_CFLAGS) -c runtime/world.c -o $(BUILD)/world_lib.o
	ar rcs $@ $(BUILD)/world_lib.o

lib/world.link: Makefile
	echo "$(GAME_LIBS) -ldl" > $@

# the videos library (`use videos`): video files, on FFmpeg's libraries
# (found with pkg-config: ffmpeg on Arch, libavformat-dev, libavcodec-dev,
# libswscale-dev and libswresample-dev on Debian, Homebrew's ffmpeg)
FFMPEG_PKGS := libavformat libavcodec libswscale libswresample libavutil
VIDEOS_LIBS := $(shell $(PKG_CONFIG) --libs $(FFMPEG_PKGS) 2>/dev/null)
VIDEOS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(FFMPEG_PKGS) 2>/dev/null)
# (without pkg-config: where Homebrew puts it, or the usual places)
ifeq ($(strip $(VIDEOS_LIBS)),)
FFMPEG_FOUND := $(patsubst %/include/libavformat/avformat.h,%,$(wildcard $(shell $(BREW) --prefix ffmpeg 2>/dev/null)/include/libavformat/avformat.h \
	/opt/homebrew/opt/ffmpeg/include/libavformat/avformat.h /usr/local/opt/ffmpeg/include/libavformat/avformat.h /usr/local/include/libavformat/avformat.h))
FFMPEG_PREFIX := $(firstword $(foreach p,$(FFMPEG_FOUND),$(call LIB_FOR_HERE,$(p)/lib/libavformat.dylib,$(p))))
ifneq ($(FFMPEG_PREFIX),)
VIDEOS_LIBS := -L$(FFMPEG_PREFIX)/lib -lavformat -lavcodec -lswscale -lswresample -lavutil
VIDEOS_CFLAGS := -I$(FFMPEG_PREFIX)/include
endif
endif
ifneq ($(strip $(VIDEOS_LIBS)),)
VIDEOS_TARGETS := lib/libcabbyvideos.so lib/libcabbyvideos.a lib/videos.link
endif
videos: $(VIDEOS_TARGETS)
ifeq ($(strip $(VIDEOS_LIBS)),)
	@echo "note: FFmpeg's libraries were not found, so the videos library was not built (install ffmpeg on Arch, libavformat-dev libavcodec-dev libswscale-dev libswresample-dev on Debian, or 'brew install ffmpeg' on a Mac)"
endif

VIDEOS_SRC := runtime/videos.c runtime/video_source.h runtime/rt.h
lib/libcabbyvideos.so: $(VIDEOS_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(VIDEOS_CFLAGS) runtime/videos.c -o $@ $(VIDEOS_LIBS) -lpthread -lm

lib/libcabbyvideos.a: $(VIDEOS_SRC) | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) $(VIDEOS_CFLAGS) -c runtime/videos.c -o $(BUILD)/videos_lib.o
	ar rcs $@ $(BUILD)/videos_lib.o

lib/videos.link: Makefile
	echo "$(VIDEOS_LIBS) -lpthread -lm" > $@

# the sound library: sound files, notes and MIDI files (miniaudio and
# TinySoundFont are built into it; it needs nothing installed)
SOUND_SRC := runtime/sound.c runtime/midi.c runtime/third_party/miniaudio.h runtime/third_party/tsf.h runtime/third_party/tml.h
lib/libcabbysound.so: $(SOUND_SRC)
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) runtime/sound.c runtime/midi.c -o $@ -lpthread -lm -ldl $(SOUND_FRAMEWORKS)

lib/libcabbysound.a: $(SOUND_SRC) | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/sound.c -o $(BUILD)/sound_lib.o
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/midi.c -o $(BUILD)/midi_lib.o
	ar rcs $@ $(BUILD)/sound_lib.o $(BUILD)/midi_lib.o

lib/sound.link: Makefile
	echo "-lpthread -ldl $(SOUND_FRAMEWORKS)" > $@

# the processes library: programs a program starts and talks to
lib/libcabbyprocesses.so: runtime/processes.c
	$(CC) -std=c11 -O2 -fPIC -shared $(PLATFORM_SHARED) $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) runtime/processes.c -o $@

lib/libcabbyprocesses.a: runtime/processes.c | $(BUILD)
	$(CC) -std=c11 -O2 -fPIC $(GUI_WARN) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/processes.c -o $(BUILD)/processes_lib.o
	ar rcs $@ $(BUILD)/processes_lib.o

lib/processes.link: Makefile
	echo "" > $@

# the runtime linked into compiled programs
lib/libcabbyrt.a: runtime/rt.c runtime/dates.c runtime/json.c runtime/csv.c runtime/networks.c runtime/markup.c runtime/bytes.c runtime/patterns.c runtime/hashes.c runtime/archives.c runtime/databases.c runtime/threads.c runtime/dbus.c runtime/bluetooth.c runtime/bluetooth_mac.h runtime/emails.c runtime/secrets.c runtime/desktop.c runtime/zones.c runtime/connections.c runtime/web.h runtime/websockets.h runtime/tls.h runtime/imap.h runtime/postgres.h runtime/images.c runtime/os.c runtime/linux.c runtime/colors.h runtime/rt.h
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/rt.c -o $(BUILD)/rt_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/dates.c -o $(BUILD)/dates_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/json.c -o $(BUILD)/json_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/networks.c -o $(BUILD)/networks_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/markup.c -o $(BUILD)/markup_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/csv.c -o $(BUILD)/csv_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/bytes.c -o $(BUILD)/bytes_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/patterns.c -o $(BUILD)/patterns_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/hashes.c -o $(BUILD)/hashes_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/archives.c -o $(BUILD)/archives_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/databases.c -o $(BUILD)/databases_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/threads.c -o $(BUILD)/threads_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/dbus.c -o $(BUILD)/dbus_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/bluetooth.c -o $(BUILD)/bluetooth_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/emails.c -o $(BUILD)/emails_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/secrets.c -o $(BUILD)/secrets_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/os.c -o $(BUILD)/os_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/linux.c -o $(BUILD)/linux_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/desktop.c -o $(BUILD)/desktop_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/zones.c -o $(BUILD)/zones_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/connections.c -o $(BUILD)/connections_lib.o
	$(CC) -std=c11 -O2 -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFS) -c runtime/images.c -o $(BUILD)/images_lib.o
	ar rcs $@ $(BUILD)/rt_lib.o $(BUILD)/dates_lib.o $(BUILD)/json_lib.o $(BUILD)/csv_lib.o $(BUILD)/networks_lib.o $(BUILD)/markup_lib.o $(BUILD)/bytes_lib.o $(BUILD)/patterns_lib.o $(BUILD)/hashes_lib.o $(BUILD)/archives_lib.o $(BUILD)/databases_lib.o $(BUILD)/threads_lib.o $(BUILD)/dbus_lib.o $(BUILD)/bluetooth_lib.o $(BUILD)/emails_lib.o $(BUILD)/secrets_lib.o $(BUILD)/desktop_lib.o $(BUILD)/zones_lib.o $(BUILD)/connections_lib.o $(BUILD)/images_lib.o $(BUILD)/os_lib.o $(BUILD)/linux_lib.o

# -rdynamic: libraries the interpreter loads (processes, tui, gui) use its
# event loop, as compiled programs use the runtime's
# (where `make install` puts the libraries, known to cabby itself too)
$(BUILD)/main.o: CFLAGS += -DCABBY_PREFIX='"$(PREFIX)"'

cabby: $(BUILD)/main.o $(CORE_O)
	$(CC) $(LDFLAGS) -rdynamic $^ $(LDLIBS) -o $@

debug:
	$(MAKE) BUILD=build-debug CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $(DBGFLAGS)" LDFLAGS="$(DBGFLAGS)" build-debug/cabby build-debug/lexdump build-debug/astdump

build-debug/cabby: build-debug/main.o $(CORE:%.c=build-debug/%.o)
	$(CC) $(LDFLAGS) -rdynamic $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tools/%.o: tools/%.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lexdump: $(BUILD)/tools/lexdump.o $(CORE_O)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/astdump: $(BUILD)/tools/astdump.o $(CORE_O)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

PREFIX ?= /usr/local

test: all
	BUILD=$(BUILD) ./tests/run_all.sh

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/cabby
	install -m 755 cabby $(DESTDIR)$(PREFIX)/bin/cabby
	install -m 644 lib/*.hby lib/*.md lib/libcabbyrt.a $(DESTDIR)$(PREFIX)/lib/cabby/
	@# libraries that were renamed or merged (string, stdio, stdlib)
	rm -f $(addprefix $(DESTDIR)$(PREFIX)/lib/cabby/,string.hby string.md stdio.hby stdio.md stdlib.hby stdlib.md)
	@# (and sdlgui, which became gui); then native code from before, so none is left over
	rm -f $(addprefix $(DESTDIR)$(PREFIX)/lib/cabby/,sdlgui.hby libcabbysdlgui.so libcabbysdlgui.a sdlgui.link)
	rm -f $(DESTDIR)$(PREFIX)/lib/cabby/libcabby*.so $(DESTDIR)$(PREFIX)/lib/cabby/*.link
	for f in $(GUI_TARGETS) $(GTK_TARGETS) $(TGAME_TARGETS) lib/libcabbytui.so lib/libcabbytui.a lib/tui.link lib/libcabbyprocesses.so lib/libcabbyprocesses.a lib/processes.link $(SOUND_TARGETS) $(GAME_TARGETS) $(WORLD_TARGETS) $(VIDEOS_TARGETS); do \
		if [ -f $$f ]; then install -m 644 $$f $(DESTDIR)$(PREFIX)/lib/cabby/; fi; done
	@# the runtime for WebAssembly (cabby -c --wasm), when make wasm has built it
	rm -rf $(DESTDIR)$(PREFIX)/lib/cabby/wasm
	if [ -f lib/wasm/libcabbyrt.a ]; then install -d $(DESTDIR)$(PREFIX)/lib/cabby/wasm && \
		install -m 644 lib/wasm/libcabbyrt.a $(DESTDIR)$(PREFIX)/lib/cabby/wasm/; fi
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/geometry
	install -m 644 README.md STYLE.md $(DESTDIR)$(PREFIX)/share/doc/cabby/
	install -m 644 tutorials/README.md tutorials/tutorials.md tutorials/*.cby $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/
	install -m 644 tutorials/geometry/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/geometry/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/reverse
	install -m 644 tutorials/reverse/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/reverse/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/pictures
	install -m 644 tutorials/pictures/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/pictures/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/models
	install -m 644 tutorials/models/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/models/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/videos
	install -m 644 tutorials/videos/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/videos/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/sounds
	install -m 644 tutorials/sounds/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/sounds/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/music $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/quest
	install -m 644 tutorials/music/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/music/
	install -m 644 tutorials/quest/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/quest/
	install -d $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/maps
	install -m 644 tutorials/maps/* $(DESTDIR)$(PREFIX)/share/doc/cabby/tutorials/maps/

# File icons for .cby and .hby, and the Cabby logo, for one user: run
# without sudo. File managers show them once the type is known (restart
# the file manager afterwards).
#
# They go in hicolor, the theme every theme falls back on, and also in the
# icon theme in use (ICON_THEME): a theme like Papirus has a plain text-file
# icon of its own, which GTK would choose before looking in hicolor.
DATADIR ?= $(HOME)/.local/share
ICON_THEME ?= Papirus-Dark
ICON_SIZES = 16x16 22x22 24x24 32x32 48x48 64x64
install-icons:
	install -d $(DATADIR)/mime/packages $(DATADIR)/icons/hicolor/scalable/mimetypes $(DATADIR)/icons/hicolor/scalable/apps
	install -m 644 icons/cabby-mime.xml $(DATADIR)/mime/packages/cabby.xml
	install -m 644 icons/text-x-cabby.svg icons/text-x-cabby-header.svg $(DATADIR)/icons/hicolor/scalable/mimetypes/
	install -m 644 icons/cabby.svg $(DATADIR)/icons/hicolor/scalable/apps/
	@for t in $(ICON_THEME); do \
		if [ -d /usr/share/icons/$$t ] || [ -d $(DATADIR)/icons/$$t ]; then \
			for s in $(ICON_SIZES); do \
				install -d $(DATADIR)/icons/$$t/$$s/mimetypes; \
				install -m 644 icons/text-x-cabby.svg icons/text-x-cabby-header.svg $(DATADIR)/icons/$$t/$$s/mimetypes/; \
			done; echo "  also in the $$t theme"; \
		fi; done
	-update-mime-database $(DATADIR)/mime
	-gtk-update-icon-cache -f -t $(DATADIR)/icons/hicolor
	@echo "Cabby's icons installed in $(DATADIR); restart the file manager to see them"

uninstall-icons:
	rm -f $(DATADIR)/mime/packages/cabby.xml $(DATADIR)/icons/hicolor/scalable/mimetypes/text-x-cabby*.svg
	rm -f $(DATADIR)/icons/hicolor/scalable/apps/cabby.svg
	for t in $(ICON_THEME); do for s in $(ICON_SIZES); do \
		rm -f $(DATADIR)/icons/$$t/$$s/mimetypes/text-x-cabby*.svg; done; done
	-update-mime-database $(DATADIR)/mime
	-gtk-update-icon-cache -f -t $(DATADIR)/icons/hicolor

# For packages (packaging/): the file icons, the logo and Vim support for
# everyone, under $(DESTDIR)$(PREFIX)/share, where the system's own file
# managers and Vim look. (The package manager updates the icon and type
# caches itself.)
install-extras:
	install -d $(DESTDIR)$(PREFIX)/share/mime/packages $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/mimetypes \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps
	install -m 644 icons/cabby-mime.xml $(DESTDIR)$(PREFIX)/share/mime/packages/cabby.xml
	install -m 644 icons/text-x-cabby.svg icons/text-x-cabby-header.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/mimetypes/
	install -m 644 icons/cabby.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/
	for d in ftdetect ftplugin indent syntax; do \
		install -d $(DESTDIR)$(PREFIX)/share/vim/vimfiles/$$d && \
		install -m 644 editor/vim/$$d/cabby.vim $(DESTDIR)$(PREFIX)/share/vim/vimfiles/$$d/; done

# Vim support, for one user: run without sudo.
VIMDIR ?= $(HOME)/.vim
install-vim:
	for d in ftdetect ftplugin indent syntax; do \
		install -d $(VIMDIR)/$$d && install -m 644 editor/vim/$$d/cabby.vim $(VIMDIR)/$$d/; done
	@echo "Vim support for Cabby installed in $(VIMDIR)"
	@# (macOS's Vim, unlike most Linux ones, starts with colors and file types off)
	@if ! grep -qsE '^[[:space:]]*(syntax[[:space:]]+(on|enable)|source[[:space:]].*defaults\.vim)' $(HOME)/.vimrc; then \
		echo "note: your ~/.vimrc doesn't turn colors on; add these two lines to it:"; \
		echo "  syntax on"; echo "  filetype plugin indent on"; fi

uninstall-vim:
	rm -f $(VIMDIR)/ftdetect/cabby.vim $(VIMDIR)/ftplugin/cabby.vim \
		$(VIMDIR)/indent/cabby.vim $(VIMDIR)/syntax/cabby.vim

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cabby
	rm -rf $(DESTDIR)$(PREFIX)/lib/cabby $(DESTDIR)$(PREFIX)/share/doc/cabby

# ---- cabby.wasm: the interpreter built for WebAssembly, to run in a
# browser (wasm/README.md). It needs wasi-sdk (WASI_SDK, below): its clang
# and its C library. No libffi there: a C function is called through a
# wrapper made for it (wasm/calls.c, from the libraries' extern lines). No
# compiling, no interactive session, and only the libraries that need
# nothing of the computer's own.
# (in ~/tools, the newest made for this computer, wasi-sdk-*-x86_64-linux
# or -arm64-macos and so on; or else the newest of any)
WASI_HOST := $(subst aarch64,arm64,$(shell uname -m))-$(subst darwin,macos,$(shell uname -s | tr A-Z a-z))
WASI_SDK ?= $(firstword $(lastword $(sort $(wildcard $(HOME)/tools/wasi-sdk-*-$(WASI_HOST)))) \
	$(lastword $(sort $(wildcard $(HOME)/tools/wasi-sdk-*))) $(wildcard /opt/wasi-sdk))
WASM_CC := $(WASI_SDK)/bin/clang
# (one for another computer, which this one can't run: say so)
ifneq ($(WASI_SDK),)
ifneq ($(filter wasm%,$(MAKECMDGOALS)),)
ifneq ($(shell $(WASM_CC) --version > /dev/null 2>&1 && echo runs),runs)
$(error $(WASM_CC) can't be run here: is it the wasi-sdk for another computer? This one wants wasi-sdk-*-$(WASI_HOST).tar.gz, from https://github.com/WebAssembly/wasi-sdk/releases, unpacked into ~/tools)
endif
endif
endif
# (none found, and asked for: say where to get it, not "no /bin/clang")
ifeq ($(WASI_SDK),)
ifneq ($(filter wasm%,$(MAKECMDGOALS)),)
$(error make $(firstword $(filter wasm%,$(MAKECMDGOALS))) needs wasi-sdk 33 or newer, a clang for WebAssembly: unpack the one for your computer (on a Mac with an Apple processor, wasi-sdk-*-arm64-macos.tar.gz) from https://github.com/WebAssembly/wasi-sdk/releases into ~/tools, or give its place: make wasm WASI_SDK=/path/to/wasi-sdk (see wasm/README.md))
endif
endif
ifneq ($(WASI_SDK),)
CFLAGS += -DCABBY_WASI_SDK='"$(WASI_SDK)"'
endif
WASM_STACK := 33554432
WASM_CFLAGS := --target=wasm32-wasip1 -std=c11 -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_GETPID \
	-mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false -DCABBY_WASM_STACK=$(WASM_STACK) -Iwasm -Iruntime
WASM_LDFLAGS := -Wl,-z,stack-size=$(WASM_STACK) -Wl,--initial-memory=268435456 -Wl,--max-memory=2147483648 \
	-lsetjmp -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-mman -lwasi-emulated-getpid -lm
WASM_SRC := main.c arena.c diag.c util.c lexer.c ast.c ast_print.c parser.c types.c style.c \
	sema_decl.c sema_expr.c sema_stmt.c sema_flow.c interpreter.c highlight.c wasm/stubs.c wasm/calls.c \
	runtime/rt.c runtime/dates.c runtime/json.c runtime/csv.c runtime/markup.c runtime/bytes.c runtime/patterns.c \
	runtime/hashes.c runtime/archives.c runtime/databases.c runtime/threads.c runtime/emails.c \
	runtime/secrets.c runtime/zones.c runtime/images.c runtime/linux.c runtime/os.c
WASM_O := $(WASM_SRC:%.c=build/wasm/%.o)

wasm: cabby.wasm lib/wasm/libcabbyrt.a

# lib/wasm/libcabbyrt.a: the runtime for programs compiled for WebAssembly
# (cabby -c --wasm), with a stand-in for each of the runtime's functions
# that isn't in it (made from the libraries' extern lines), so that a
# program using one still builds, and says so if it's called
WASM_RT_O := $(filter build/wasm/runtime/%,$(WASM_O))
lib/wasm/libcabbyrt.a: $(WASM_RT_O) build/wasm/rt-missing.o
	@mkdir -p lib/wasm
	rm -f $@
	$(WASI_SDK)/bin/llvm-ar rcs $@ $^
build/wasm/rt-missing.c: tools/make-wasm-calls.py $(WASM_RT_O) $(wildcard lib/*.hby)
	$(WASI_SDK)/bin/llvm-nm --defined-only -j $(WASM_RT_O) | sort -u > build/wasm/rt-defined.txt
	python3 tools/make-wasm-calls.py --missing build/wasm/rt-defined.txt $@ lib/*.hby
build/wasm/rt-missing.o: build/wasm/rt-missing.c
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

# lib/wasm/libcabbygame.a and libcabbygui.a: the game and gui libraries for
# WebAssembly, drawn by SDL in software and shown by the page
# (wasm/README.md), when SDL has been built for WebAssembly
# (tools/build-sdl-wasm.sh, into build/wasm-sdl)
WASM_SDL := build/wasm-sdl
ifneq ($(wildcard $(WASM_SDL)/lib/libSDL3.a),)
wasm: lib/wasm/libcabbygame.a lib/wasm/libcabbygui.a lib/wasm/libSDL3.a
endif
lib/wasm/libSDL3.a: $(WASM_SDL)/lib/libSDL3.a
	@mkdir -p lib/wasm
	cp $< $@
build/wasm/game.o: runtime/game.c $(GUI_SRC)
	@mkdir -p build/wasm
	$(WASM_CC) $(WASM_CFLAGS) -I$(WASM_SDL)/include -c runtime/game.c -o $@
lib/wasm/libcabbygame.a: build/wasm/game.o
	@mkdir -p lib/wasm
	rm -f $@
	$(WASI_SDK)/bin/llvm-ar rcs $@ $^
build/wasm/gui.o: runtime/gui.c $(GUI_SRC)
	@mkdir -p build/wasm
	$(WASM_CC) $(WASM_CFLAGS) -I$(WASM_SDL)/include -c runtime/gui.c -o $@
lib/wasm/libcabbygui.a: build/wasm/gui.o
	@mkdir -p lib/wasm
	rm -f $@
	$(WASI_SDK)/bin/llvm-ar rcs $@ $^

# build/wasm-site: what a web page needs to run Cabby (wasm/README.md):
# cabby.wasm, cabby.js, cabby-worker.js, the libraries' headers in one
# file, and a page to try it (wasm/index.html)
wasm-site: cabby.wasm
	@mkdir -p build/wasm-site
	cp cabby.wasm wasm/cabby.js wasm/cabby-worker.js wasm/index.html build/wasm-site/
	mkdir -p build/wasm-site/fonts && cp wasm/fonts/DejaVuSans.ttf wasm/fonts/LICENSE build/wasm-site/fonts/
	python3 -c 'import json, glob, os; json.dump({"/lib/" + os.path.basename(f): open(f).read() for f in sorted(glob.glob("lib/*.hby"))}, open("build/wasm-site/cabby-lib.json", "w"))'
	@echo "build/wasm-site: open it through a web server (python3 -m http.server -d build/wasm-site)"
	@$(if $(WASM_GAME),true,echo "  (without windows and games: SDL isn't built for WebAssembly; tools/build-sdl-wasm.sh, then make wasm-site again)")
# (with the game and gui libraries and SDL, when SDL is built for
# WebAssembly: then `use game` and `use gui` run interpreted in a browser too)
# (and the runtime's stand-ins, for what those use that isn't here: D-Bus)
WASM_GAME := $(if $(wildcard $(WASM_SDL)/lib/libSDL3.a),build/wasm/game.o build/wasm/gui.o $(WASM_SDL)/lib/libSDL3.a \
	build/wasm/rt-missing.o)
cabby.wasm: $(WASM_O) $(WASM_GAME)
	$(WASM_CC) --target=wasm32-wasip1 $(WASM_O) $(WASM_GAME) $(WASM_LDFLAGS) -o $@

build/wasm/%.o: %.c
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

wasm/calls.c: tools/make-wasm-calls.py $(wildcard lib/*.hby) $(wildcard tutorials/*.cby)
	WASI_CLANG=$(WASM_CC) WASI_CFLAGS="--target=wasm32-wasip1 -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE" \
		python3 tools/make-wasm-calls.py $@ lib/*.hby tutorials/*.cby tests/*.cby tests/*/*.cby

clean:
	rm -rf build build-debug cabby cabby.wasm lib/wasm lib/libcabbyrt.a lib/libcabbygui.so lib/libcabbygui.a lib/gui.link lib/libcabbytui.so lib/libcabbytui.a lib/tui.link $(PROC_TARGETS) $(SOUND_TARGETS) lib/libcabbygame.so lib/libcabbygame.a lib/game.link lib/libcabbygtk.so lib/libcabbygtk.a lib/gtk.link lib/gtk.hby lib/libcabbytgame.so lib/libcabbytgame.a lib/tgame.link lib/tgame.hby lib/libcabbysdlgui.so lib/libcabbysdlgui.a lib/sdlgui.link lib/sdlgui.hby lib/libcabbyworld.so lib/libcabbyworld.a lib/world.link lib/libcabbyvideos.so lib/libcabbyvideos.a lib/videos.link

-include $(wildcard $(BUILD)/*.d $(BUILD)/tools/*.d $(BUILD)/runtime/*.d)
