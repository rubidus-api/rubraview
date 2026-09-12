# Third-Party Notices

Rubraview ships as a single executable that statically links the
libraries below. Their licence texts travel with the binary; the full
provenance record for each is in `docs/resources/sources-and-licenses.md`.

## proven_c_lib

- Vendored snapshot at `vendor/proven/`
- Licence: MIT — see `vendor/proven/LICENSE`
- Used for: memory arenas, UTF-8 string slices, dynamic collections, and
  the job scheduler behind the pre-cache worker.
- Additional notices from that project: `vendor/proven/THIRD_PARTY_NOTICES.md`

## miniz 3.1.2

- Vendored snapshot at `vendor/miniz/`
- Copyright 2013-2014 RAD Game Tools and Valve Software; copyright
  2010-2014 Rich Geldreich and Tenacious Software LLC
- Licence: MIT — see `vendor/miniz/LICENSE`
- Used for: DEFLATE decompression of CBZ (ZIP) entries only. The ZIP
  container itself is parsed by rubraview's own reader.

## LZMA SDK 24.08

- Vendored snapshot at `vendor/lzma/` (decode subset; see
  `vendor/lzma/VENDORED.md` for exactly which files and why)
- Written and placed in the **public domain** by Igor Pavlov. Some of it
  is based on public-domain code by other authors: PPMd var.H (2001) by
  Dmitry Shkarin.
- Licence text: `vendor/lzma/LICENSE.txt`
- Used for: reading 7z (CB7) comic archives, including the solid blocks
  §3.8.2 needs a persistent decoder for. No encoder, no file I/O and no
  encryption support is included.
- Public domain imposes no condition; this notice is credit, not
  compliance.

## libjpeg-turbo 3.0.4

- Vendored snapshot at `vendor/libjpeg-turbo/` (the libjpeg API library
  only; see `vendor/libjpeg-turbo/VENDORED.md`)
- This software is based in part on the work of the Independent JPEG
  Group. Copyright (C) 1991-2020, Thomas G. Lane, Guido Vollbeding;
  libjpeg-turbo modifications copyright (C) 2009-2024, D. R. Commander
  and others.
- Licences: the IJG License (`vendor/libjpeg-turbo/README.ijg`) and the
  Modified 3-clause BSD License (`vendor/libjpeg-turbo/LICENSE.md`).
  Both are attribution-only.
- Used for: rotating and flipping JPEGs by rearranging their DCT
  coefficients, and stripping metadata, without decoding any pixel. No
  encoder, no file I/O, no SIMD and no arithmetic coding is included.

Nothing else in the tree is third-party code. The Windows platform
libraries (Direct2D, DirectWrite, the Windows Imaging Component, WASAPI)
are operating-system components, not bundled dependencies.

## FFmpeg (headers only, not shipped)

rubraview can play media through FFmpeg when its DLLs are present beside
the executable. To compile those calls, the public headers of FFmpeg
8.1.2 are kept in `vendor/ffmpeg/include/`; they are licensed
LGPL-2.1-or-later (`vendor/ffmpeg/COPYING.LGPLv2.1`).

No FFmpeg code is compiled into rubraview and none is distributed with
it: the functions are looked up at run time from DLLs the user provides.
Windows Media Foundation remains the default decoder (D-8).

