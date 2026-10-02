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
    RAR15_ERR_NOMEM = -1,
    RAR15_ERR_TRUNCATED = -2 /* the stream needs bits past its data area (spec 3) */
};

/* A new decoder (64 KiB window). NULL when out of memory. */
rar15_unpacker *rar15_new(void);
void rar15_free(rar15_unpacker *u);

/* Decode one file. `packed` is the file's data area (for a file split over
   volumes, the parts' data areas joined, spec 2). Writes exactly `unp_size`
   bytes to `out`. `solid` nonzero continues the state left by the previous
   file of a solid archive (spec 6.3); zero starts afresh. Returns RAR15_OK or a
   negative RAR15_ERR_* value; after an error the state is unusable until the
   next call with solid == 0. Files stored (METHOD 0x30) are not passed here. */
int rar15_unpack(rar15_unpacker *u, const uint8_t *packed, size_t packed_size,
                 uint8_t *out, uint64_t unp_size, int solid);

#endif
