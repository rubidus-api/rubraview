# Vendored bzip2 (decoder only, framing changed for ALZ)

A snapshot of libbzip2's decompressor, changed to read ALZ's cut-down bzip2
streams (`docs/specs/alz-format.md` §3.2). It no longer reads standard `.bz2`
streams. Altered source, marked as such, as the bzip2 licence asks.

- Upstream: https://sourceware.org/bzip2/
- Release: `1.0.8` (13 July 2019)
- Archive: `bzip2-1.0.8.tar.gz`, SHA-256
  `ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269`
- Retrieved: 2026-10-01
- Licence: bzip2 licence, BSD-style (see `LICENSE`, copied verbatim from the archive)

Copied from the archive: `bzlib.c`, `bzlib.h`, `bzlib_private.h`,
`decompress.c`, `huffman.c`, `crctable.c`, `randtable.c`, `LICENSE`.
Not copied: the compressor (`compress.c`, `blocksort.c`), the programs and
the documentation.

## Changes

Every change is marked `RUBRAVIEW CHANGE` in the source. `bzlib.h`,
`bzlib_private.h`, `huffman.c`, `crctable.c` and `randtable.c` are unchanged.

- `decompress.c` (spec §3.2): no `BZh` stream header, the block size is
  always 9; the block magic is the 32 bits `"DLZ" 01` and the end-of-stream
  magic `"DLZ" 02`, read from the bit stream, any other fourth byte an
  error; no block CRC and no randomised bit after the block magic; no
  combined CRC after the end magic.
- `bzlib.c`: the per-block and combined CRC comparisons are commented out
  (ALZ stores neither; the file's CRC-32 is checked by `src/core/alz.c`), and
  the compression side and `BZ2_bzBuffToBuffCompress` are left out (`#if 0`).

## How it is used

Built with `-DBZ_NO_STDIO -DBZ_EXPORT` (no stdio, plain functions on
Windows too), warnings off and sanitisers on, like the other vendored
decoders. Only `src/core/alz.c` includes `bzlib.h`, and it supplies
`bz_internal_error`.

## Updating

Replace the files from a newer release's archive, apply the changes above
again, keep the markers, and update the version, hash and date here.
