# Vendored: dav1d 1.5.4 (the C code)

| | |
|---|---|
| Upstream | https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz — the same `src/` and `include/` as tag `1.5.4` of https://code.videolan.org/videolan/dav1d |
| SHA-256 of the release tarball | `686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5` (equal to the `.sha256` published beside it) |
| Retrieved | 2026-10-12 |
| Licence | **BSD-2-Clause** (`COPYING`) — verified by reading it in the retrieved tarball; shipped here |
| Modification | none to any upstream file. `config/host/config.h`, `config/win/config.h` and `config/vcs_version.h` are **new files written for this build** (see below) |
| Ledger row | R016 |

## Why it is here

AVIF pictures are AV1 frames. Windows decodes them only where the Store's
AV1 and HEIF extensions are installed, and then without the alpha (RV-110,
D-107). dav1d decodes them everywhere; `vendor/libavif` reads the
container round it. Nothing else calls dav1d: films are played by Media
Foundation or FFmpeg as before.

## What was taken, and what was left

- `src/*.c`, `src/*.h`, `src/win32/thread.c`, and the headers of `src/x86`
  (included by the C code; no function of theirs is used with `HAVE_ASM 0`).
- `include/common`, `include/compat`, `include/dav1d`.
- `COPYING`.

Left out on purpose: **all assembly** (`src/x86/*.asm`, `src/arm`,
`src/loongarch`, `src/ppc`, `src/riscv`) — the owner decided against an
assembler in the build (2026-10-12), so this dav1d is several times slower
than a usual one; the tools, the tests, the examples, the documentation
and the meson files.

## The files written for this build

Upstream's meson generates `config.h` and `vcs_version.h`. This project
does not use meson, so the two `config.h` are what meson 's
`-Denable_asm=false` configuration wrote on the build machine — one for
Linux (the host tests), one for the mingw-w64 cross build — kept as they
came, and `vcs_version.h` names the version.

## How it is built

By this project's `Makefile` with the flags meson uses (`-std=c99 -O2
-DNDEBUG -D_GNU_SOURCE -fomit-frame-pointer -ffast-math`); the thirteen
`*_tmpl.c` files twice, with `-DBITDEPTH=8` and `-DBITDEPTH=16`.

## Updating

Take the same directories from the new release, compare with the tag,
configure it once with meson (`-Denable_asm=false`) to see whether
`config.h` gained or lost a line, update this file and R016, and run
`build/tests/test_avif_read`.
