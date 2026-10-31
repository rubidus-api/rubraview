/* compat.h - FultaArc: the Rubraview core helper the ported ALZ reader uses (Rubraview's core.h, MIT). Strings are
 * proven's `proven_u8str_view_t`; Rubraview's own copy of that type was dropped (D-028). */
#ifndef FULTA_ALZ_COMPAT_H
#define FULTA_ALZ_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "proven.h"

static inline proven_result_mem_mut_t fa_alz_arena_alloc_array(proven_arena_t *arena, proven_size_t count,
                                                               proven_size_t size) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, count, size)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
    return proven_arena_alloc(arena, bytes);
}

#endif
