/* fa_alloc.h - FultaArc: allocation through proven's allocator trait (D-038). Shared by the core and by the ported
 * RAR and ALZ modules, which do not include internal.h. MIT. */
#ifndef FA_ALLOC_H
#define FA_ALLOC_H

#include "proven/allocator.h"
#include "proven/heap.h"
#include "proven/u8str.h"

#include <stddef.h>

/* One fixed allocation alignment: fundamental alignment, which covers every string, buffer, byte array and codec
 * state. A block is allocated, reallocated and freed at this one class, as proven's allocator trait requires. */
#define FA_ALIGN (alignof(max_align_t))

/* Allocation through proven's allocator trait (D-038). Every block names the allocator it lives in: an archive's
 * (`arc->alloc`: the caller's fulta_arc_allocator_t, else proven's heap), a decoder's (`limits->alloc`, the same),
 * a writer's (the write options' allocator, else proven's heap). A block is resized and released only through the
 * allocator that made it, with its true old size (proven's trait requires it for non-heap allocators). NULL on
 * failure; n == 0 gives a valid one-byte block (the trait refuses size 0). */
void *fa_alloc(proven_allocator_t a, size_t n);
void *fa_zalloc(proven_allocator_t a, size_t count, size_t n);   /* zeroed; checks the multiplication */
void *fa_resize(proven_allocator_t a, void *p, size_t old_n, size_t new_n);   /* p NULL: allocates */
void fa_release(proven_allocator_t a, void *p);                   /* NULL is a no-op */
char *fa_strdup_view(proven_allocator_t a, proven_u8str_view_t s); /* NUL-terminated copy */
char *fa_strndup_a(proven_allocator_t a, const char *s, size_t n);  /* the first n bytes, NUL-terminated */

#endif /* FA_ALLOC_H */
