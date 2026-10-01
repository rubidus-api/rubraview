/*
 * unrar_proprietary — the module's export, for a program that links it
 * rather than loading the DLL (Rubraview's host tests). The table is
 * include/rubraview/rar_codec.h's. UnRAR licence: see unrar_proprietary.c.
 */
#ifndef UNRAR_PROPRIETARY_H
#define UNRAR_PROPRIETARY_H

#include "rubraview/rar_codec.h"

const rubraview_rar_codec_t *unrar_proprietary_api(uint32_t version);

#endif /* UNRAR_PROPRIETARY_H */
