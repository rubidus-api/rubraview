CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -pedantic -Iinclude -Ivendor/proven/include -Ivendor/proven/platform -g -fsanitize=address,undefined
LDFLAGS ?= -lm

SRCS_CORE = src/core/pixbuf.c src/core/color.c src/core/resample.c src/core/filter.c
SRCS_PROVEN = vendor/proven/src/proven/arena.c \
              vendor/proven/src/proven/memory.c \
              vendor/proven/src/proven/panic.c \
              vendor/proven/platform/proven_sys_mem.c

TEST_BINS = build/tests/test_pixbuf build/tests/test_color build/tests/test_resample build/tests/test_filters

.PHONY: all test check clean win64

all: test

build/tests/%: tests/%.c $(SRCS_CORE) $(SRCS_PROVEN)
	@mkdir -p build/tests
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

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

win64:
	@echo "Cross-building Windows x86_64 target (run via linux-build container per REMOTE.md)"
	@mkdir -p dist
	x86_64-w64-mingw32-gcc -std=c23 -O2 \
		-Iinclude -Ivendor/proven/include -Ivendor/proven/platform \
		$(SRCS_CORE) $(SRCS_PROVEN) \
		-ld2d1 -ldwrite -lole32 -lwindowscodecs -lshcore \
		-o dist/rubraview.exe

clean:
	rm -rf build/ dist/
