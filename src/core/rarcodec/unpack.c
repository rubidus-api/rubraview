/*
 * Rubraview's RAR codec (MIT): unpacking. Written from docs/specs/rar-decompression.md.
 */
#include "rc_internal.h"
#include <stdlib.h>

void *rc_unpack_create(void) { return calloc(1, 1); }
void rc_unpack_destroy(void *unpack) { free(unpack); }

rubraview_rar_unpack_status_t rc_unpack_file(void *unpack, const rubraview_rar_unpack_params_t *params,
                                             rubraview_rar_read_fn read, void *read_ctx,
                                             rubraview_rar_write_fn write, void *write_ctx) {
    (void)unpack; (void)params; (void)read; (void)read_ctx; (void)write; (void)write_ctx;
    return RUBRAVIEW_RAR_UNPACK_UNSUPPORTED;
}
