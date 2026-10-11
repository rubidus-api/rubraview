# Vendored: libwebp 1.6.0 (libwebp, libwebpmux, libsharpyuv)

| | |
|---|---|
| Upstream | https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz — the same sources as tag `v1.6.0` of https://github.com/webmproject/libwebp |
| SHA-256 of the release tarball | `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564` |
| Checked | `src/`, `sharpyuv/`, `COPYING` and `PATENTS` are byte for byte those of GitHub's archive of the tag (the release only adds what autotools generates); this box has no `gpg` for the `.asc` |
| Retrieved | 2026-10-12 |
| Licence | **BSD-3-Clause** (`COPYING`) with Google's additional patent grant (`PATENTS`) — verified by reading both in the retrieved tarball; both are shipped here |
| Modification | none to any upstream file |
| Ledger row | R015 |

## Why it is here

Windows reads WebP through a system codec and has no encoder for it.
Export, Save a copy and `--batch --format=webp` write WebP with this
library (RV-109, D-106; owner, 2026-10-11 and 2026-10-12), a still
picture or an animation. `src/core/webp_write.c` is the only file that
calls it.

The licence is permissive and attribution-only; it does not affect the
project's own MIT licence. Its notices are in `THIRD_PARTY_NOTICES.md`.

**The viewer still reads WebP through WIC.** The decoder is here because
the animation encoder (`src/mux/anim_encode.c`) calls it to look at the
frames it has written, and the host test reads back what was written.

## What was taken, and what was left

- `src/enc`, `src/dec`, `src/dsp`, `src/utils`: libwebp.
- `src/mux`: libwebpmux (the container and `WebPAnimEncoder`).
- `sharpyuv`: libsharpyuv, which the encoder links.
- `src/webp`: the public headers of those (`demux.h` is not taken).
- `COPYING`, `PATENTS`, `AUTHORS`.

Left out on purpose: `src/demux` (nothing reads an animation with this
library), the programs and their readers and writers (`examples`,
`imageio`, `extras`), the bindings (`swig`, `webp_js`), the tests, the
documentation, and every build file (`Makefile.am`, `*.rc`,
`config.h.in`, CMake, autotools).

## How it is built

By this project's `Makefile`, into `build/libwebp.a` (host, for the
tests) and `build/libwebp-win.a`, with `-Ivendor/libwebp` and no
`config.h`: the sources then decide by the compiler alone. On x86-64 that
is the C code and the SSE2 code (always there); the SSE4.1 and AVX2 files
compile to nothing without their flags, and the NEON, MIPS and MSA files
likewise. No threads (`WEBP_USE_THREAD` is not defined).

## Updating

Take the same directories from the new release, compare with the tag on
GitHub, update this file, `docs/resources/sources-and-licenses.md` (R015)
and the notices, and run `build/tests/test_webp_write`.
