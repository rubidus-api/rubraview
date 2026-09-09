# Sources And Licenses

Detailed provenance, copyright, license, attribution, and redistribution notes.

Before using third-party code, images, audio, fonts, datasets, text, or other resources, verify the license and record it here.

## R000: Project Resources Initialized

- Source:
- Author:
- License:
- Retrieved:
- Local path:
- Distribution: no
- Modification:
- Attribution:
- Notes:

## R001: miniz 3.1.2

- Source: https://github.com/richgel999/miniz, release `3.1.2`, archive
  `miniz-3.1.2.zip` (SHA-256 `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`)
- Author: Rich Geldreich and Tenacious Software LLC; RAD Game Tools and Valve Software
- License: MIT, read from the `LICENSE` file inside the retrieved archive
  and copied verbatim to `vendor/miniz/LICENSE`. Compatible with
  redistributing rubraview under MIT; the notice ships with the binary.
- Retrieved: 2026-09-09
- Local path: `vendor/miniz/` (`miniz.c`, `miniz.h`, `LICENSE`, `readme.md`)
- Distribution: yes — statically linked into `rubraview.exe`
- Modification: none. The files are byte-identical to the release
  archive; configuration happens at the include site in
  `src/core/archive.c`, not by editing the vendored source.
- Attribution: `THIRD_PARTY_NOTICES.md` and `vendor/miniz/LICENSE`
- Notes: Admitted under owner decision D-2 (2026-09-08) and the
  RFC-0001 §8.1 rule for vendored libraries. Used only for DEFLATE
  decompression (RV-051); the ZIP container parsing remains rubraview's
  own §3.8.1 central-directory reader. Provenance details and the
  update procedure are in `vendor/miniz/VENDORED.md`.

## R002: 7-Zip LZMA SDK 24.08

- Source: https://www.7-zip.org/sdk.html — release `lzma2408.7z`, retrieved from the project's SourceForge mirror
- Author: Igor Pavlov (PPMd var.H: Dmitry Shkarin)
- License: **Public domain — verified 2026-09-09** by reading `DOC/lzma-sdk.txt` in the retrieved archive; copied to `vendor/lzma/LICENSE.txt`
- SHA-256 (release archive): `105a12afcafcd5bdce70bc75e7f0e94eafd07293646278ea225e6601e048cf17`
- Retrieved: 2026-09-09
- Local path: vendor/lzma/ (decode subset only — see `vendor/lzma/VENDORED.md`)
- Distribution: yes — statically linked into `rubraview.exe`; licence text shipped
- Modification: none — files are byte-identical to the release
- Attribution: licence notice in `THIRD_PARTY_NOTICES.md`
- Notes: 7z / CB7 decoding including solid blocks (RV-052, D-3). Admitted under RFC-0001 §8.1 (owner decision 2026-09-08). Encoders, `7zFile.c` and the AES/SHA-256 sources are deliberately not vendored; an encrypted archive therefore surfaces as an unsupported coder.

## R003: libjpeg-turbo (planned — not yet vendored)

- Source: (fill at vendoring: upstream URL and tag)
- Author: (fill at vendoring)
- License: BSD-3-Clause + IJG + zlib (expected; verify LICENSE.md — three notices, all must ship)
- Retrieved: (fill at vendoring)
- Local path: vendor/ (planned)
- Distribution: yes — statically linked into `rubraview.exe`; licence text shipped
- Modification: none intended; allocator routed to `prv_arena_t` through the library's hook if available
- Attribution: licence notice in `THIRD_PARTY_NOTICES.md`
- Notes: DCT-domain lossless rotate/flip only; no pixel decoding (RV-069, D-5). Admitted under RFC-0001 §8.1 (owner decision 2026-09-08). Fill the empty fields and change the heading from "planned" before the first commit that adds the files.
