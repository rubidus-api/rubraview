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

## R001: miniz (planned — not yet vendored)

- Source: (fill at vendoring: upstream URL and tag)
- Author: (fill at vendoring)
- License: MIT (expected; verify the LICENSE file in the retrieved tree)
- Retrieved: (fill at vendoring)
- Local path: vendor/ (planned)
- Distribution: yes — statically linked into `rubraview.exe`; licence text shipped
- Modification: none intended; allocator routed to `prv_arena_t` through the library's hook if available
- Attribution: licence notice in `THIRD_PARTY_NOTICES.md`
- Notes: inflate for CBZ entries (RV-051, D-2). Admitted under RFC-0001 §8.1 (owner decision 2026-09-08). Fill the empty fields and change the heading from "planned" before the first commit that adds the files.

## R002: 7-Zip LZMA SDK (planned — not yet vendored)

- Source: (fill at vendoring: upstream URL and tag)
- Author: (fill at vendoring)
- License: Public domain (expected; verify `lzma-sdk` DOC/lzma.txt)
- Retrieved: (fill at vendoring)
- Local path: vendor/ (planned)
- Distribution: yes — statically linked into `rubraview.exe`; licence text shipped
- Modification: none intended; allocator routed to `prv_arena_t` through the library's hook if available
- Attribution: licence notice in `THIRD_PARTY_NOTICES.md`
- Notes: 7z / CB7 decoding incl. solid streams (RV-052, D-3). Admitted under RFC-0001 §8.1 (owner decision 2026-09-08). Fill the empty fields and change the heading from "planned" before the first commit that adds the files.

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
