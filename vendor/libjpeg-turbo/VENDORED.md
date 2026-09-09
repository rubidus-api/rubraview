# Vendored: libjpeg-turbo 3.0.4 (coefficient API only)

| | |
|---|---|
| Upstream | https://github.com/libjpeg-turbo/libjpeg-turbo, tag `3.0.4` |
| SHA-256 of the release tarball | `0270f9496ad6d69e743f1e7b9e3e9398f5b4d606b6a47744df4b73df50f62e38` |
| Retrieved | 2026-09-09 |
| Licence | **IJG License + Modified (3-clause) BSD** — verified by reading `LICENSE.md` and `README.ijg` in the retrieved tarball; both are shipped here |
| Modification | none to any upstream file. `jconfig.h`, `jconfigint.h` and `jversion.h` are **new files written for this build** (see below) |
| Ledger row | R003 |

## Why it is here

Rotating a JPEG by decoding and re-encoding it loses quality every time,
because the second encode quantises an already-quantised picture. A JPEG
can instead be rotated by rearranging its DCT coefficient blocks — no
pixel is touched, and the result is bit-exact. That is what §3.9 asks for
and what owner decision D-5 approved this library for: the `jpegtran`
coefficient path, through `transupp.c`.

Both licences are permissive and attribution-only, so they do not affect
the project's own MIT licence. Their notices are in
`THIRD_PARTY_NOTICES.md`.

**WIC remains the only pixel codec in this program.** Nothing in
`src/core/jpegtran.c` decodes an image.

## What was taken, and what was left

The libjpeg API library only: `jc*`, `jd*`, `jf*`, `ji*`, `jerror`,
`jmemmgr`, `jmemnobs`, `jutils`, `jquant*`, `jpeg_nbits`, `jstdhuff`, and
`transupp.c` with its header.

Left out on purpose:

- **the programs** (`cjpeg`, `djpeg`, `jpegtran`, `rdjpgcom`,
  `wrjpgcom`), the file readers and writers (`rd*.c`, `wr*.c`) and
  `cdjpeg.c` — this is a library, not a command-line tool;
- **the TurboJPEG wrapper** (`turbojpeg.c`, `tj*`) and its JNI bindings —
  a second API over the same library, which nothing here calls;
- **the SIMD assembly** — the coefficient path runs no DCT or IDCT, so
  there is nothing for it to accelerate, and it would have made the
  build depend on `nasm`;
- **arithmetic coding** (`jcarith.c`, `jdarith.c`, `jaricom.c`) — rare
  in practice; a JPEG that uses it is refused rather than mishandled.

## The three hand-written files

Upstream generates `jconfig.h`, `jconfigint.h` and `jversion.h` from
`.in` templates with CMake. This project does not use CMake, so they are
written out here with the choices made explicitly: 8-bit samples,
in-memory source and destination managers on, arithmetic coding off,
SIMD off. `jversion.h` is the upstream template with its one substitution
applied.

## The three precisions

libjpeg-turbo 3.x picks sample precision at run time, which it implements
by compiling one subset of its sources **three times** with
`BITS_IN_JSAMPLE` set to 8, 12 and 16. The library does not link without
all three, even for a program that only ever opens 8-bit JPEGs, so the
`Makefile` builds `build/libjpeg`, `build/libjpeg12` and
`build/libjpeg16`. Six `.c` files upstream are `#include` fragments
rather than translation units and must not be compiled on their own; the
`Makefile` filters them out by name.

## Updating

1. Fetch the new tag, record its SHA-256 and the date above.
2. Copy the same file list; do not add the tools or TurboJPEG.
3. Re-read `LICENSE.md`; re-derive `jversion.h` from the new template and
   check the `.in` templates for new options.
4. Re-check the `JPEG12_NAMES` / `JPEG16_NAMES` lists in the `Makefile`
   against upstream's `CMakeLists.txt` — they change between releases.
5. `make test` (T042 rotates a real JPEG four times and compares bytes)
   and `make win64`.
