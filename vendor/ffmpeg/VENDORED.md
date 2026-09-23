# FFmpeg headers (vendored)

Only the public headers, and only so that rubraview can *call* FFmpeg when
the user puts its DLLs beside the executable (D-8, RV-016). No FFmpeg code
is compiled into or shipped with rubraview: every function is looked up at
run time with `GetProcAddress`.

- Upstream: https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
  (SHA-256 `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`,
  retrieved 2026-09-24). ffmpeg.org publishes no checksum file beside it,
  and this box has no `gpg` for the `.asc`, so the headers were checked
  against the same tag on GitHub (`n9.0.2`): all five libraries' public
  headers are byte for byte identical in both copies.
- Contents: the headers each library's `Makefile` installs, for libavutil,
  libavcodec, libavformat, libswscale and libswresample, unmodified.
- Added here, not from the tarball: `include/libavutil/avconfig.h`, which
  FFmpeg's `configure` generates (x86-64 values), because the headers are
  vendored without building FFmpeg.
- Licence: LGPL-2.1-or-later (`COPYING.LGPLv2.1`). The GPL-only parts of
  FFmpeg are not here and are not used.
- **The version matters.** These headers describe FFmpeg 9.0's structures.
  `pal_media_ffmpeg.c` checks the DLLs' own version at run time and refuses
  a different major version rather than reading their memory wrongly.

## Updating

Copy the public headers of the new release the same way, update this file
and `docs/resources/sources-and-licenses.md` (R005), and change the major
versions the loader accepts.
