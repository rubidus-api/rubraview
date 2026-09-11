# DECISIONS

This is the single append-only accepted decision log.

Use this for non-secret accepted project decisions that should remain traceable.

Read this file only when the current task needs prior decisions, decision rationale, or supersession history.

Do not split decisions into current and old files. If a decision is superseded, append a new entry and mark the older entry as superseded or superseded-by.

Do not store credentials, private infrastructure details, personal data, private remote URLs, or private-only business context here. Put private decisions in the sibling private repository when one is used.

## 2026-09-07: Foundation on Pure C23 and Vendored proven_c_lib

- Status: Accepted
- Context: Rubraview requires high performance, predictable memory ownership, and minimal external library bloat. The workspace maintains `proven_c_lib` as the standard C23 base library.
- Decision: Base all dynamic memory, arena allocations, UTF-8 strings (`u8str`), and dynamic array collections on a vendored snapshot of `proven_c_lib` under `vendor/proven/`. Core modules will adhere to ISO C23 (`-std=c23`).
- Consequences: Complete avoidance of heap fragmentation; high predictability; no dependency on complex C++ runtimes or heavy external utility frameworks.
- Supersedes: None

## 2026-09-07: Direct2D and Windows Imaging Component (WIC) for Presentation and Image Codecs

- Status: Accepted
- Context: The application requires zero third-party image decoding dependencies and butter-smooth 60–144 FPS canvas manipulation on 4K/8K displays. Legacy GDI software rasterization (`StretchBlt`) causes high CPU load and frame drop.
- Decision: Use Direct2D (D2D1) / Direct3D 11 for hardware-accelerated viewport rendering, pan/zoom affine transforms, and real-time shader adjustment previews. Use the Windows Imaging Component (WIC) COM API for decoding and encoding JPEG, PNG, GIF, WebP, TIFF, BMP, and ICO.
- Consequences: Eliminates third-party image libraries (`libpng`, `libjpeg`, etc.); provides native GPU pan/zoom and real-time adjustment previews with zero external DLLs.
- Supersedes: None

## 2026-09-07: Dynamic FFmpeg C API PAL Bridge for Universal Video Playback

- Status: Accepted
- Context: Windows Media Foundation (WMF) lacks out-of-the-box support for widely used media formats (MKV, WebM, FLV, HEVC without MS Store extension). An image/media viewer must reliably open any dropped media file.
- Decision: Implement an isolated Platform Abstraction Layer (PAL) bridge around FFmpeg C APIs (`libavcodec`, `libavformat`, `libswscale`). The bridge uses dynamic library loading so Rubraview launches cleanly even if FFmpeg DLLs are absent, while unlocking universal playback, frame-accurate seeking, and frame capture when present.
- Consequences: Provides universal format compatibility and frame stepping; keeps video dependencies strictly isolated behind PAL interfaces.
- Supersedes: None

## 2026-09-07: Headless-First Dual Build Architecture

- Status: Accepted
- Context: Fast iteration requires automated unit testing on Linux developer workstations, while final delivery targets Windows x86_64 binaries.
- Decision: Decouple the Core Engine (pixel math, resampling, convolution, batch queue) from Win32 APIs. The test suite compiles and runs natively on Linux host (`make test`). Windows binaries cross-compile using MinGW-w64 on `linux-build` (`make win64`).
- Consequences: Enables continuous integration and AddressSanitizer testing directly on Linux; prevents regression in image processing algorithms.
- Supersedes: None

## 2026-09-08: Roadmap Decisions D-1..D-7 (RFC-0001 §11.4)

- Status: Accepted
- Context: RFC-0001 §11 was rewritten into milestones M0–M9 with a traceability table; seven choices could not be made by the plan itself.
- Decision:
  - D-1 Windows x86_64 only until 1.0; §8 multi-platform PAL stays as intent; PAL headers carry no Win32 types.
  - D-2 `deflate` for CBZ via vendored `miniz` (MIT).
  - D-3 CB7 via the vendored 7-Zip LZMA SDK (public domain); CBR is post-1.0.
  - D-4 No Windows Media Foundation fallback; FFmpeg is the only decode path.
  - D-5 Lossless JPEG rotation via vendored `libjpeg-turbo` coefficient transforms (BSD-3-Clause + IJG + zlib); WIC remains the only pixel codec.
  - D-6 Music player is post-1.0; 1.0 plays audio files with album art from the playlist (RV-084).
  - D-7 `-Werror` is added to the build now (RV-009).
  - Rule: a vendored C library is admitted when its licence is redistributable alongside MIT, it is wrapped in one `rubraview_` module, and it has a `docs/resources/` ledger entry (RFC-0001 §8.1).
- Consequences: three third-party sources will enter `vendor/` with licence texts shipped; one decode path for media; the 1.0 scope is a Windows image/comic viewer with video playback.
- Supersedes: None (refines 2026-09-07 "Direct2D and WIC" — WIC stays the only *pixel* codec; DCT-domain transforms are not pixel decoding).

## 2026-09-11: D-8 Media Foundation First, FFmpeg as a Replaceable Second Backend

- Status: Accepted
- Context: D-4 made FFmpeg the only decode path, and M5 stalled on it: FFmpeg's headers are LGPL and were not on the build machine. The owner reviewed the Windows built-in decoders instead. `rubraview-mfprobe` on Windows 11 (build 26200) found Media Foundation decoders for H.264, VP8, VP9, AV1, MPEG-1/2/4, H.263, DV, WMV3, VC-1 and Motion JPEG, and for AAC, MP3, WMA, FLAC, ALAC, Vorbis, Opus and AC-3. HEVC and Theora were absent; HEVC needs a paid ($0.99) Microsoft Store extension.
- Decision: Windows Media Foundation is the default decode path. FFmpeg is a second backend behind the same PAL interface, loaded dynamically at run time and replaceable; its headers may be vendored in the tree (owner, 2026-09-11). How a file reaches FFmpeg — a setting, an automatic fallback, or both — is settled in the M5 plan (`docs/plans/active/2026-09-11-m5-decoder.md`).
- Consequences: common formats play with no DLL beside the executable; formats Media Foundation lacks need FFmpeg DLLs or a codec extension. RV-016/054/055/056/062 are reworded around a backend-neutral PAL. Statements that FFmpeg is the only path (SPEC §5, the video requirement, RFC-0001 §5.2) are updated when the plan is confirmed.
- Supersedes: D-4 (2026-09-08). Refines 2026-09-07 "Dynamic FFmpeg C API PAL Bridge for Universal Video Playback", which remains true of the FFmpeg backend.

## 2026-09-11: D-9 How Media Reaches Each Backend (M5 plan answers)

- Status: Accepted
- Context: D-8 left the swap between Media Foundation and FFmpeg to the M5 plan; the plan put four questions to the owner.
- Decision:
  - A setting names the preferred backend, and a file the preferred backend cannot open is retried on the other one automatically.
  - A file neither backend can open is reported on screen and the viewer moves on to the next file.
  - For HEVC without the Store extension the message names both remedies: the $0.99 Microsoft Store extension, or FFmpeg DLLs beside the executable.
  - Test media are public sample files whose licence has been checked; they stay out of git and are recorded in the resource ledger.
- Consequences: the backend-selection policy lives in `src/core` and is host-tested; the OSD message table gains the "cannot open" and HEVC entries.
- Supersedes: None (completes D-8).
