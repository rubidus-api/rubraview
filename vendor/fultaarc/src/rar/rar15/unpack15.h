/* unpack15.h - decompressor for the RAR 1.5 compressed stream (methods 0x31..0x35 of RAR 1.55 archives).
 *
 * Written from docs/spec/rar15.md (section 2, and 1.6/1.7 for volumes and solid archives); section numbers in the
 * comments of unpack15.c refer to that specification.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of
 * the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Usage:
 *   rar15_state *st = malloc(sizeof *st);
 *   rar15_init(st);                                  - before the first file, and before every file of a
 *                                                      non-solid archive
 *   rar15_unpack(st, packed, packed_len, out, size, NULL); - one file; in a solid archive call it again for the next
 *                                                      file without rar15_init (the state carries over, 1.7)
 * The packed bytes of a file split over volumes are passed as one buffer (1.6). After an error the state is not
 * usable for further files until rar15_init.
 */
#ifndef UNPACK15_H
#define UNPACK15_H

#include <stddef.h>
#include <stdint.h>

enum rar15_status {
    RAR15_OK = 0,
    RAR15_E_ARG = 1,        /* a null pointer where data is needed */
    RAR15_E_TRUNCATED = 2,  /* the stream needs bits beyond the packed bytes (2.1) */
    RAR15_E_UNOBSERVED = 3  /* a code that RAR's own archives never hold (code index 256: spec 2.6, 2.9, 2.10) */
};

typedef struct rar15_list {
    uint16_t sym[256];      /* symbol at each position */
    int32_t cnt[256];       /* use count of each symbol */
    int32_t lim;            /* reset limit (2.3) */
} rar15_list;

/* The whole decoder state (2.4). Fields are internal; the struct is public so that callers can place it. */
typedef struct rar15_state {
    uint8_t win[65536];     /* window: the last 65536 output bytes */
    uint64_t total;         /* bytes ever written to the window */
    rar15_list lit, flag, block;
    uint16_t dlist[256];    /* Q distance list */
    int32_t S, kv, ky, kd, qs, kK, ph;
    uint64_t nA;
    uint32_t rep, run;
    uint8_t fbyte, fleft;   /* pending flag bits: the low `fleft` bits of fbyte, MSB first */
    bool lmode, swapped, kpost, pfar, has_last, far;
    uint32_t recent[4];
    uint32_t last_n, last_d;
} rar15_state;

void rar15_init(rar15_state *st);

/* Decode one file: `in`/`in_len` its packed bytes, `out` receives exactly `size` bytes. Returns a rar15_status.
 * `produced` (may be null) receives the number of output bytes written before the return (= size on success). */
int rar15_unpack(rar15_state *st, const uint8_t *in, size_t in_len, uint8_t *out, size_t size, size_t *produced);

#endif
