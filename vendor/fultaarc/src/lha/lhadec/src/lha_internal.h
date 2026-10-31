/*
 * lhadec - internal interfaces.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 FultaArc contributors
 */
#ifndef LHA_INTERNAL_H
#define LHA_INTERNAL_H

#include "../include/lhadec.h"

/* Buffered input over the user's read callback. */
typedef struct lha_in {
    lha_read_fn rd;
    void *ctx;
    unsigned char *buf;
    size_t cap, pos, len;        /* buf[pos..len) is unread */
    uint64_t offset;             /* source offset of buf[pos] */
    int eof, err;
} lha_in;

size_t lha_in_need(lha_in *in, size_t n);     /* make up to n bytes available; returns bytes available */

/* A byte source limited to the packed data of one entry. */
#define LHA_SRC_END (-1)
#define LHA_SRC_ERROR (-2)

typedef struct lha_src {
    lha_in *in;
    uint64_t remaining;          /* bytes of this entry not yet handed out */
} lha_src;

int lha_src_getc(lha_src *s);
size_t lha_src_read(lha_src *s, unsigned char *dst, size_t n);  /* (size_t)-1 on I/O error */

/* A sink that computes the CRC-16 and counts bytes before passing them on. */
typedef struct lha_sink {
    lha_write_fn wr;             /* may be NULL (test only) */
    void *ctx;
    uint16_t crc;
    uint64_t count;
} lha_sink;

int lha_sink_write(lha_sink *k, const unsigned char *p, size_t n);

int lha_decode_method(lha_method m, lha_src *src, lha_sink *sink, uint64_t osize);

#endif
