# Vendored: libavif 1.4.2 (the reader)

| | |
|---|---|
| Upstream | https://github.com/AOMediaCodec/libavif, tag `v1.4.2` (tag object `bd32fb40a3b2028eb555b67f769c57a32ed8a1b4`) |
| SHA-256 of GitHub's archive of the tag | `2b645287340ba5a631d268b551dc2d72bd73ac33335962dd36dcdb6d8366921d` |
| Retrieved | 2026-10-12 |
| Licence | **BSD-2-Clause**; the files under `third_party/libyuv` **BSD-3-Clause** — both texts are in `LICENSE`, read in the retrieved archive and shipped here |
| Modification | none to any upstream file |
| Ledger row | R017 |

## Why it is here

It reads the AVIF container — the items, the alpha plane, grids, the
frames of a sequence and their times, the rotation and mirroring — and
turns dav1d's planes into RGB (RV-110, D-107). `src/core/avif_read.c` is
the only file that calls it.

## What was taken, and what was left

- `src/*.c` except `write.c` and the codecs other than `codec_dav1d.c`.
- `include/avif/avif.h`, `include/avif/internal.h`.
- `third_party/libyuv` (the part of libyuv libavif carries for scaling).
- `LICENSE`.

Left out on purpose: the encoder (`write.c`) and every encoding codec —
nothing writes AVIF; `third_party/iccjpeg`; the programs, the tests, the
bindings, CMake.

## How it is built

By this project's `Makefile`, with `-DAVIF_CODEC_DAV1D=1` and nothing
else switched on, into one static library with dav1d.

## Updating

Take the same files from the new tag, update this file and R017 and the
notices, and run `build/tests/test_avif_read` (its orientation fixtures
were written by Pillow through libavif: all eight must still agree).
