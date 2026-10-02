/* RAR 1.5 decompression (unpack version 15). MIT licence (see LICENSE).
   Written from docs/specs/rar15.md (rar15-blackbox clean room), which was worked
   out from RAR 1.55's behaviour; container framing: docs/specs/rar-decompression.md
   section 2, CRC-32 section 5.1.

   The entry point is a proposal for the RAR decoder's side (questions file, Q2):
   one decoder state per archive, kept across the files of a solid archive. */
#ifndef RUBRAVIEW_RARCODEC_UNPACK15_H
#define RUBRAVIEW_RARCODEC_UNPACK15_H

#include <stddef.h>
#include <stdint.h>

typedef struct rar15_unpacker rar15_unpacker;

enum {
    RAR15_OK = 0,
    RAR15_STOPPED,      /* the writer returned false (rar15_unpack_stream) */
    RAR15_OVERRUN,      /* read further past the packed data than `max_overrun_bits` allows */
};

/* A new decoder (64 KiB window). NULL when out of memory. */
rar15_unpacker *rar15_new(void);
void rar15_free(rar15_unpacker *u);

/* Decode one file. `packed` is the file's data area (for a file split over
   volumes, the parts' data areas joined, spec 2). Writes exactly `unp_size`
   bytes to `out`. `solid` nonzero continues the state left by the previous
   file of a solid archive (spec 6.3); zero starts afresh. Bits past the data
   area read as 0 (spec 3), so every stream decodes: the caller checks FILE_CRC
   to catch damage, as RAR does. Returns RAR15_OK. Files stored (METHOD 0x30)
   are not passed here. */
int rar15_unpack(rar15_unpacker *u, const uint8_t *packed, size_t packed_size,
                 uint8_t *out, uint64_t unp_size, int solid);

/* Streaming form (rar-decoder session, 2026-10-02): the same decoding, with the
   packed bytes pulled from `read` (as many as fit, 0 at the end) and the output
   pushed to `write` in pieces (false stops it: RAR15_STOPPED). Exactly `unp_size`
   bytes are written. Bits past the end of the input read as 0 (spec 3), as in
   rar15_unpack; with `max_overrun_bits` below UINT64_MAX the call gives up with
   RAR15_OVERRUN once it has read that many bits past the end, so that a damaged
   header's huge size does not turn a short stream into gigabytes of zeros.
   rar15_unpack is this call on memory, with no limit. */
typedef size_t (*rar15_read_fn)(void *ctx, uint8_t *buffer, size_t capacity);
typedef int (*rar15_write_fn)(void *ctx, const uint8_t *data, size_t size);   /* nonzero: go on */

int rar15_unpack_stream(rar15_unpacker *u, rar15_read_fn read, void *read_ctx, rar15_write_fn write,
                        void *write_ctx, uint64_t unp_size, int solid, uint64_t max_overrun_bits);

#endif
