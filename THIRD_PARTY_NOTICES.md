# Third-Party Notices

Rubraview ships as a single executable that statically links the
libraries below. Their licence texts travel with the binary; where each
came from is in its `vendor/<name>/VENDORED.md`.

## proven_c_lib

- Vendored snapshot at `vendor/proven/`
- Licence: MIT — see `vendor/proven/LICENSE`
- Used for: memory arenas, UTF-8 string slices, dynamic collections, and
  the job scheduler behind the pre-cache worker; FultaArc is built on it
  too.
- Additional notices from that project: `vendor/proven/THIRD_PARTY_NOTICES.md`

## FultaArc 0.1.0

- Vendored snapshot at `vendor/fultaarc/` (see `vendor/fultaarc/VENDORED.md`)
- Licence: MIT — see `vendor/fultaarc/LICENSE`
- Used for: reading every archive (ZIP / CBZ, 7z / CB7, RAR / CBR 2.0 to
  7.0, with their solid, split and password-protected forms) and writing
  ZIP and 7z. Its RAR decoder was written in a clean room from a
  specification; it contains no UnRAR code.
- It brings the parts below with it; its own list, with the Unicode
  licence's text, is `vendor/fultaarc/THIRD_PARTY_NOTICES.md`.

### LZMA SDK 24.08 (inside FultaArc)

- At `vendor/fultaarc/vendor/lzma/`
- Written and placed in the **public domain** by Igor Pavlov. Some of it
  is based on public-domain code by other authors: PPMd var.H (2001) by
  Dmitry Shkarin.
- Licence text: `vendor/fultaarc/vendor/lzma/LICENSE.txt`
- Used for: LZMA, LZMA2, PPMd and the 7z filters.

### miniz 3.1.2 (inside FultaArc)

- At `vendor/fultaarc/vendor/miniz/`
- Copyright 2013-2014 RAD Game Tools and Valve Software; copyright
  2010-2014 Rich Geldreich and Tenacious Software LLC
- Licence: MIT — see `vendor/fultaarc/vendor/miniz/LICENSE`
- Used for: DEFLATE when writing ZIP.

### bzip2 1.0.8 (inside FultaArc)

- At `vendor/fultaarc/vendor/bzip2/`, unmodified
- Copyright (C) 1996-2019 Julian R Seward
- Licence: bzip2 licence (BSD-style) — see `vendor/fultaarc/vendor/bzip2/LICENSE`
- Used for: bzip2-compressed ZIP and 7z entries.

### Zstandard 1.5.7 decoder (inside FultaArc)

- At `vendor/fultaarc/vendor/zstd/`
- Copyright (c) Meta Platforms, Inc. and affiliates
- Licence: BSD-3-Clause — see `vendor/fultaarc/vendor/zstd/LICENSE`
  (upstream is dual BSD / GPLv2; the BSD licence is the one taken)
- Used for: zstd-compressed ZIP and 7z entries.

### Unicode mapping data (inside FultaArc)

- Generated into `vendor/fultaarc/src/text/cp_tables.c` from the Unicode
  Consortium's mapping files (code pages 437, 866, 1251, 1252, 932, 936,
  949, 950; KOI8-R)
- Licence: Unicode License v3 — its text is in
  `vendor/fultaarc/THIRD_PARTY_NOTICES.md`
- Used for: file names that an archive stores in a legacy code page.

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

