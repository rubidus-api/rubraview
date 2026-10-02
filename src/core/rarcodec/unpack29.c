/*
 * Rubraview's RAR codec (MIT): the RAR 2.9 / 3.x algorithm. Written from docs/specs/rar-decompression.md.
 */
#include "rc_unpack.h"

rubraview_rar_unpack_status_t rc_unpack29(rc_unpack_t *u, bool solid, bool drain) {
    (void)u; (void)solid; (void)drain;
    return RUBRAVIEW_RAR_UNPACK_UNSUPPORTED;
}
void rc_unpack29_free(rc_unpack_t *u) { (void)u; }
