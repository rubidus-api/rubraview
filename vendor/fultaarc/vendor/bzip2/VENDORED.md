# Vendored bzip2 (standard, decoder only)

- Upstream: https://sourceware.org/bzip2/ , release 1.0.8 (13 July 2019)
- Archive: `bzip2-1.0.8.tar.gz`, SHA-256 `ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269`,
  downloaded 2026-10-03 from https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
- Licence: bzip2 licence, BSD-style (`LICENSE`, verbatim)
- Copied: `bzlib.c`, `bzlib.h`, `bzlib_private.h`, `decompress.c`, `compress.c`, `blocksort.c`, `huffman.c`,
  `crctable.c`, `randtable.c` — unmodified. The compressor files are there because `bzlib.c` refers to them;
  FultaArc only decompresses. Built with `-DBZ_NO_STDIO`.
- Used for: ZIP method 12, 7z `040202`, EGG method 2. ALZ's cut-down framing is the separate copy in
  `../bzip2_alz/` (D-005 #2).
