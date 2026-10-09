/* compat.h - FultaArc: the Rubraview core definitions the ported ALZ reader uses (Rubraview's core.h, MIT). */
#ifndef FULTA_ALZ_COMPAT_H
#define FULTA_ALZ_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "proven.h"

typedef struct u8str {
    const char   *ptr;
    proven_size_t len;
} u8str_t;

static inline proven_result_mem_mut_t fa_alz_arena_alloc_array(proven_arena_t *arena, proven_size_t count,
                                                               proven_size_t size) {
    proven_size_t bytes = 0;
    if (PROVEN_CKD_MUL(&bytes, count, size)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_OVERFLOW };
    return proven_arena_alloc(arena, bytes);
}

#endif
