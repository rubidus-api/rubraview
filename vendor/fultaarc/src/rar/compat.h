/* compat.h - FultaArc: the few Rubraview core helpers the ported RAR reader uses (Rubraview's
 * include/rubraview/core.h, MIT, same author), so that the clean-room code keeps its shape (D-006). Strings are
 * proven's `proven_u8str_view_t`; Rubraview's own copy of that type was dropped (D-028). */
#ifndef FULTA_RAR_COMPAT_H
#define FULTA_RAR_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "proven.h"

static inline proven_result_mem_mut_t fa_rar_arena_alloc_array(proven_arena_t *arena, proven_size_t count,
                                                               proven_size_t size) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, count, size)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
    return proven_arena_alloc(arena, bytes);
}

static inline proven_byte_t fa_rar_ascii_lower(proven_byte_t c) {
    return (c >= 'A' && c <= 'Z') ? (proven_byte_t)(c - 'A' + 'a') : c;
}

/* ASCII letters ignore case; every other byte must match exactly. */
static inline bool fa_rar_u8_eq_lit_ci(proven_u8str_view_t s, const char *lit) {
    size_t n = strlen(lit);
    if (s.size != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (fa_rar_ascii_lower(s.ptr[i]) != fa_rar_ascii_lower((proven_byte_t)lit[i])) return false;
    return true;
}

#endif
