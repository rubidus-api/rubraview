#ifndef FA_RARCODEC_H
#define FA_RARCODEC_H

#include "rar_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rubraview's own RAR decoder (MIT; src/core/rarcodec/), written from
 * docs/specs/rar-decompression.md in the rar-decoder clean-room session. The
 * program registers it at start: fa_rar_set_codec(fa_rarcodec()).
 */
const fa_rar_codec_t *fa_rarcodec(void);

#ifdef __cplusplus
}
#endif

#endif /* FA_RARCODEC_H */
