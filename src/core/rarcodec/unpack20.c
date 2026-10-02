/*
 * Rubraview's RAR codec (MIT): the RAR 2.0 algorithm. Written from docs/specs/rar-decompression.md.
 */
#include "rc_unpack.h"

rubraview_rar_unpack_status_t rc_unpack20(rc_unpack_t *u, bool solid, uint64_t dest) {
    (void)u; (void)solid; (void)dest;
    return RUBRAVIEW_RAR_UNPACK_UNSUPPORTED;
}
