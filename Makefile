CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -pedantic -Werror -Iinclude -Ivendor/proven/include -Ivendor/proven/platform -g -fsanitize=address,undefined
LDFLAGS ?= -lm -lpthread

SRCS_CORE = src/core/pixbuf.c src/core/color.c src/core/resample.c src/core/filter.c src/core/path.c src/core/sort.c \
            src/core/utf8.c src/core/glob.c src/core/ini.c src/core/nfc.c src/core/encoding.c \
            src/core/viewport.c src/core/layout.c src/core/archive.c src/core/comicinfo.c \
            src/core/lru.c src/core/exif.c src/core/keymap.c src/core/slideshow.c src/core/batch.c \
            src/core/playlist.c src/core/compositor.c src/core/transform.c src/core/ui_input.c src/core/ui_box.c src/core/ui_menu.c src/core/ui_chrome.c src/core/ui_virtual.c src/core/filmstrip.c src/core/picker.c src/core/default_keymap.c src/core/history.c src/core/pagesource.c src/core/precache.c src/core/animation.c src/core/sevenzip.c
# miniz is third-party and does not build clean under this project's
# -Werror -pedantic settings, so it is compiled separately with warnings
# off. It is still instrumented by the sanitisers on the host build:
# inflate runs on hostile input (§10.2), which is exactly where ASan
# earns its place. Only decompression is enabled — the ZIP container is
# parsed by rubraview's own reader (§3.8.1).
MINIZ_DEFINES = -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS \
                -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
MINIZ_INCLUDE = -Ivendor/miniz
MINIZ_OBJ = build/miniz.o
MINIZ_OBJ_WIN = build/miniz-win.o

# The LZMA SDK (public domain) supplies the 7z container parser and the
# LZMA/LZMA2/PPMd/BCJ decoders for CB7 (owner decision D-3). Same
# treatment as miniz: warnings off because it is not our code, sanitisers
# on because it parses untrusted input. Z7_ST builds the single-threaded
# decoder — the viewer already has its own worker pool and does not want
# the SDK starting threads of its own.
LZMA_DEFINES = -DZ7_ST -DZ7_PPMD_SUPPORT
LZMA_INCLUDE = -Ivendor/lzma
LZMA_SRCS = $(wildcard vendor/lzma/*.c)
LZMA_OBJS = $(patsubst vendor/lzma/%.c,build/lzma/%.o,$(LZMA_SRCS))
LZMA_OBJS_WIN = $(patsubst vendor/lzma/%.c,build/lzma-win/%.o,$(LZMA_SRCS))

SRCS_PROVEN = vendor/proven/src/proven/arena.c \
              vendor/proven/src/proven/memory.c \
              vendor/proven/src/proven/panic.c \
              vendor/proven/src/proven/job.c \
              vendor/proven/platform/proven_sys_mem.c \
              vendor/proven/platform/proven_sys_thread.c

# Portable logic layered on the PAL; compiled into both builds.
SRCS_PAL_COMMON = src/pal/pal_fs_common.c

# Host (Linux) PAL: real POSIX filesystem and clock, inert stubs for the
# window/render/image backends that only exist on Windows.
SRCS_PAL_HOST = src/pal/host/pal_fs_posix.c \
                src/pal/host/pal_time_posix.c \
                src/pal/host/pal_file_dialog_host.c

# Windows PAL: Win32, Direct2D, and WIC backends.
SRCS_PAL_WIN32 = src/pal/win32/pal_fs_win32.c \
                 src/pal/win32/pal_time_win32.c \
                 src/pal/win32/pal_window_win32.c \
                 src/pal/win32/pal_render_d2d.c \
                 src/pal/win32/pal_image_wic.c \
                 src/pal/win32/pal_file_dialog_win32.c

SRCS_APP = src/app/main.c

TEST_BINS = build/tests/test_pixbuf build/tests/test_color build/tests/test_resample build/tests/test_filters build/tests/test_path build/tests/test_sort \
            build/tests/test_utf8 build/tests/test_glob build/tests/test_ini build/tests/test_nfc build/tests/test_encoding \
            build/tests/test_viewport build/tests/test_layout build/tests/test_archive build/tests/test_comicinfo \
            build/tests/test_lru build/tests/test_exif build/tests/test_keymap build/tests/test_slideshow build/tests/test_batch \
            build/tests/test_playlist build/tests/test_pal_fs build/tests/test_pal_time build/tests/test_compositor build/tests/test_transform build/tests/test_ui_input build/tests/test_ui_box build/tests/test_ui_chrome build/tests/test_ui_browse build/tests/test_default_keymap build/tests/test_history build/tests/test_pagesource build/tests/test_precache build/tests/test_animation build/tests/test_sevenzip

.PHONY: all test check clean win64

all: test

$(MINIZ_OBJ): vendor/miniz/miniz.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -w $(MINIZ_DEFINES) $(MINIZ_INCLUDE) -g -fsanitize=address,undefined -c $< -o $@

# The alignment check is the one sanitiser turned off here. The SDK
# reads 32- and 64-bit fields straight out of a byte buffer, which is
# deliberate in its design and harmless on x86-64, but it is still
# undefined behaviour and it drowns the real findings. Everything else —
# ASan, and the rest of UBSan — stays on, because this code parses
# untrusted input.
build/lzma/%.o: vendor/lzma/%.c
	@mkdir -p build/lzma
	$(CC) -std=c11 -O2 -w $(LZMA_DEFINES) $(LZMA_INCLUDE) -g -fsanitize=address,undefined -fno-sanitize=alignment -c $< -o $@

build/tests/%: tests/%.c $(SRCS_CORE) $(SRCS_PAL_COMMON) $(SRCS_PAL_HOST) $(SRCS_PROVEN) $(MINIZ_OBJ) $(LZMA_OBJS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS) $(MINIZ_DEFINES) $(MINIZ_INCLUDE) $(LZMA_INCLUDE) $^ $(LDFLAGS) -o $@

test: $(TEST_BINS)
	@echo "=== Running Rubraview Core Unit Tests ==="
	@for t in $(TEST_BINS); do \
		echo "Running $$t..."; \
		$$t || exit 1; \
	done
	@echo "All tests passed successfully!"

check:
	@sh scripts/project-check.sh
	@sh scripts/context-budget.sh

MINGW_CC ?= x86_64-w64-mingw32-gcc

$(MINIZ_OBJ_WIN): vendor/miniz/miniz.c
	@mkdir -p build
	$(MINGW_CC) -std=c11 -O2 -w $(MINIZ_DEFINES) $(MINIZ_INCLUDE) -c $< -o $@

build/lzma-win/%.o: vendor/lzma/%.c
	@mkdir -p build/lzma-win
	$(MINGW_CC) -std=c11 -O2 -w $(LZMA_DEFINES) $(LZMA_INCLUDE) -c $< -o $@

win64: $(MINIZ_OBJ_WIN) $(LZMA_OBJS_WIN)
	@echo "Cross-building Windows x86_64 target"
	@mkdir -p dist
	$(MINGW_CC) -std=c23 -O2 -Wall -Wextra -Werror -municode -mwindows \
		$(MINIZ_DEFINES) $(MINIZ_INCLUDE) $(LZMA_INCLUDE) \
		-Iinclude -Ivendor/proven/include -Ivendor/proven/platform \
		$(SRCS_CORE) $(SRCS_PAL_COMMON) $(SRCS_PAL_WIN32) $(SRCS_APP) $(SRCS_PROVEN) $(MINIZ_OBJ_WIN) $(LZMA_OBJS_WIN) \
		-ld2d1 -ldwrite -lole32 -loleaut32 -luuid -lwindowscodecs -lshcore -ldwmapi -lshell32 -lgdi32 \
		-o dist/rubraview.exe
	@echo "Linked: dist/rubraview.exe"

clean:
	rm -rf build/ dist/
