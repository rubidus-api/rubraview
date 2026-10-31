/* lhadec_shim.h - FultaArc: the build environment of the vendored lhadec sources (the two .c files in
 * src/lha/lhadec/src), which stay
 * byte-identical to the LHA clean room's deliverable (pinned by SHA-256, tests/tools/lhadec-pin.sh). Force-included
 * by the Makefile when, and only when, those two files are compiled. It does two things and nothing else:
 *  - routes lhadec's libc allocator calls onto FultaArc's fa_malloc family (D-021: allocations funnel onto proven's
 *    allocator trait; on Windows that is the _aligned_* family, so all four calls must be mapped together). The
 *    fa_* functions accept realloc(NULL, n) and free(NULL) as libc does, which lhadec relies on. The largest block
 *    is the history ring: 16 KiB (-lh4-/-lh5-), 64 KiB (-lh6-), 128 KiB (-lh7-/-lk7-), 1 MiB (-lhx-) per the room's
 *    lzh-methods.md §2.1, allocated per extracted entry; with the 64 KiB input buffer and fixed tables these
 *    stay outside the codec memory budget (max_memory), as deflate's window does -- a stated choice (lha.md);
 *  - renames lhadec's link symbols (lhadec_rename.h).
 * <stdlib.h> is included first so its declarations are not rewritten; lhadec's own later #include <stdlib.h> is then
 * a no-op and only its calls meet the macros. */
#ifndef FULTA_LHADEC_SHIM_H
#define FULTA_LHADEC_SHIM_H
#include <stddef.h>
#include <stdlib.h>

/* the four seam functions, declared as in src/core/internal.h (lhadec's sources do not include that header) */
void *fa_malloc(size_t n);
void *fa_calloc(size_t count, size_t n);
void *fa_realloc(void *p, size_t n);
void fa_free(void *p);

#define malloc(n)     fa_malloc(n)
#define calloc(c, n)  fa_calloc((c), (n))
#define realloc(p, n) fa_realloc((p), (n))
#define free(p)       fa_free(p)

#include "lhadec_rename.h"
#endif
