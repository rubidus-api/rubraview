# FFmpeg headers (vendored)

Only the public headers, and only so that rubraview can *call* FFmpeg when
the user puts its DLLs beside the executable (D-8, RV-016). No FFmpeg code
is compiled into or shipped with rubraview: every function is looked up at
run time with `GetProcAddress`.

- Upstream: https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz
  (SHA-256 `464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`,
  retrieved 2026-09-12)
- Contents: the headers each library's `Makefile` installs, for libavutil,
  libavcodec, libavformat, libswscale and libswresample, unmodified.
- Added here, not from the tarball: `include/libavutil/avconfig.h`, which
  FFmpeg's `configure` generates (x86-64 values), because the headers are
  vendored without building FFmpeg.
- Licence: LGPL-2.1-or-later (`COPYING.LGPLv2.1`). The GPL-only parts of
  FFmpeg are not here and are not used.
- **The version matters.** These headers describe FFmpeg 8.1's structures.
  `pal_media_ffmpeg.c` checks the DLLs' own version at run time and refuses
  a different major version rather than reading their memory wrongly.

## Updating

Copy the public headers of the new release the same way, update this file
and `docs/resources/sources-and-licenses.md` (R005), and change the major
versions the loader accepts.
