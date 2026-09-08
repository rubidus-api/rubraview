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

Nothing else in the tree is third-party code. The Windows platform
libraries (Direct2D, DirectWrite, the Windows Imaging Component, WASAPI)
are operating-system components, not bundled dependencies.
