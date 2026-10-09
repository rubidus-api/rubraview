# Vendored bzip2 with ALZ's framing (decoder only) — altered source

A snapshot of libbzip2 1.0.8's decompressor changed to read ALZ's cut-down bzip2 streams (`codec-bzip2.md` §8,
`alz.md` §6). It does **not** read standard `.bz2` streams; that is `../bzip2/`.

- Origin: Rubraview's `vendor/bzip2` (the `alz-decoder` clean room, 2026-10-01), imported unchanged; upstream
  release 1.0.8, `bzip2-1.0.8.tar.gz` SHA-256 `ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269`.
- Licence: bzip2 licence, BSD-style (`LICENSE`, verbatim). Every change to the upstream files is marked
  `RUBRAVIEW CHANGE` in the source, as the licence asks for altered versions.
- Changes (from the alz-decoder session): `decompress.c` — no `BZh` header (block size 9), block and end magics are
  the 32 bits `"DLZ" 01` / `"DLZ" 02`, no block CRC, no randomised bit, no combined CRC; `bzlib.c` — the CRC
  comparisons are commented out and the compression side is left out.
- Added by FultaArc (2026-10-03, D-005 #2): `alz_bzip2_rename.h` renames every exported symbol to `ALZBZ2_*` (and
  `bz_internal_error` to `alzbz_internal_error`); it is force-included when these files are compiled. Users include
  `alz_bzlib.h`, never `bzlib.h`.
