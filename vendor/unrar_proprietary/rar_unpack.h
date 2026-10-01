/*
 * rar_unpack — RAR decompression (RAR 1.5, 2.x, 3.x/4.x and 5.0/7.0
 * algorithms), converted to C for Rubraview from UnRAR 7.3.1's source
 * (unpack*.cpp, rarvm.cpp, getbits.*) by RARLAB, 2026-09-30.
 *
 * UnRAR source code may be used in any software to handle RAR archives
 * without limitations free of charge, but cannot be used to develop RAR
 * (WinRAR) compatible archiver and to re-create RAR compression algorithm,
 * which is proprietary. Distribution of modified UnRAR source code in
 * separate form or as a part of other software is permitted, provided that
 * full text of this paragraph, starting from "UnRAR source code" words, is
 * included in license, or in documentation if license is not available,
 * and in source code comments of resulting package.
 *
 * RAR 3.x's PPMd blocks are decoded by the LZMA SDK's public-domain PPMd
 * var.H (Ppmd7.c with 7-Zip's Ppmd7aDec.c), as 7-Zip does.
 */
#ifndef RAR_UNPACK_H
#define RAR_UNPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The packed bytes come from `read` (as many as fit, 0 at the end) and the
   unpacked ones go to `write` (false stops the unpacking early). */
typedef size_t (*rar_read_fn)(void *ctx, uint8_t *buffer, size_t capacity);
typedef bool (*rar_write_fn)(void *ctx, const uint8_t *data, size_t size);

typedef struct rar_unpack rar_unpack_t;

rar_unpack_t *rar_unpack_create(void);
void rar_unpack_destroy(rar_unpack_t *u);

/* Largest dictionary accepted, bytes: larger ones are refused rather than
   allocated (a viewer has no business with a 4 GB window). */
#define RAR_UNPACK_MAX_DICT ((uint64_t)1 << 30)

/*
 * Unpack one file. `method` is 15, 20, 26, 29, 36 (the header's version)
 * or 50 / 70 (RAR5, RAR7); `solid` continues the previous file's window;
 * `dict_size` is the header's dictionary; `dest_size` the unpacked size
 * (the output is cut there). False when the method is unknown, the
 * dictionary too large, memory short, or the data broken beyond use; what
 * was written before that stays written.
 */
bool rar_unpack_file(rar_unpack_t *u, unsigned method, bool solid, uint64_t dict_size, uint64_t dest_size,
                     rar_read_fn read, void *read_ctx, rar_write_fn write, void *write_ctx);

#ifdef __cplusplus
}
#endif

#endif /* RAR_UNPACK_H */
