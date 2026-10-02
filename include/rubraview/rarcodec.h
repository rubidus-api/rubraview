#ifndef RUBRAVIEW_RARCODEC_H
#define RUBRAVIEW_RARCODEC_H

#include "rubraview/rar_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rubraview's own RAR decoder (MIT; src/core/rarcodec/), written from
 * docs/specs/rar-decompression.md in the rar-decoder clean-room session. The
 * program registers it at start: rubraview_rar_set_codec(rubraview_rarcodec()).
 */
const rubraview_rar_codec_t *rubraview_rarcodec(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_RARCODEC_H */
