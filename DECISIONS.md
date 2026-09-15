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

## 2026-09-13: D-10 What "track switching" covers in M5

- Status: Accepted
- Context: §3.16.2 and R135 ask for audio and subtitle track switching "without halting playback", and §3.16.1 also lists subtitle streams carried inside the container. Media Foundation does not decode text subtitle streams at all — it has no reader for SubRip or SubStation inside MKV or MP4 — and FFmpeg's subtitle side is a separate set of functions (`avcodec_decode_subtitle2` and its own packet path) from the audio and video ones already loaded. Listing a track nobody can display is worse than not listing it.
- Decision: M5 ships (a) the container's own track list from both backends, (b) switching between the file's sound tracks, (c) switching between the subtitle *files* beside the video, with "off" in the cycle. Subtitle streams carried inside the container are not listed and not shown; they become a later FFmpeg-only piece of work.
- Consequences: `rubraview_pal_media_tracks` and `rubraview_pal_media_select_audio_track` join the media PAL; the track list a reader sees mixes the container's sound tracks with the folder's subtitle files, which is what `rubraview_track_set_t` was already shaped for. A file whose only subtitles are embedded shows none, and says so.
- Supersedes: None (completes the track half of D-8's plan).

## 2026-09-13: D-11 Hardware decode is its own piece of work, and it will be zero-copy

- Status: Accepted (owner, 2026-09-13: "C→B")
- Context: M5's last slice was RV-062, hardware decode. Three shapes were put to the owner (`docs/plans/active/2026-09-13-m5-slice5-gpu.md`): (a) decode on the GPU but read the frame back to system memory — one file changes, no PAL boundary moves, and the read-back eats much of the gain; (b) zero copy — the renderer's D3D11 device is lent to the media backend and a decoded frame is drawn as it lies, which is the real feature and the largest single change M5 would make; (c) leave it out of M5 and do it when there is a machine to measure it on. The Win11 VM has no GPU: a build that accelerates and a build that quietly falls back to software are indistinguishable there, and "is it faster, does it still look right" cannot be answered at all.
- Decision: (c) now, (b) later. M5 closes without RV-062; when RV-062 is built it is the zero-copy path, not the read-back compromise.
- Consequences: M5 is complete as of 2026-09-13 except T058, which needs an audio device. RFC-0001 §11's M5 entry and the §11.3 row for §5.7 are amended to match. RV-062 needs a Windows machine with a GPU that can be reached the way the VM is reached, or the owner running T065 by hand; the plan file stays in `docs/plans/active/` as its specification.
- Supersedes: None.

## 2026-09-13: D-12 Text subtitle streams inside a file are shown after all (amends D-10)

- Status: Accepted
- Context: D-10 left container subtitle streams out of M5 because Media Foundation cannot decode them and avcodec's subtitle API looked like another symbol set to load. Measuring the packets showed the second half of that is not true for text formats: a Matroska `S_TEXT/UTF8` packet *is* the line, verbatim UTF-8, with the timing in the packet — `ffmpeg -map 0:s:0 -c copy -f data -` on the test file printed the words themselves. ASS wraps the line behind eight comma-separated fields and MP4 text behind a two-byte length; neither needs a decoder either.
- Decision: the FFmpeg backend lists a file's *text* subtitle streams as tracks and reads one on demand by walking the container a second time and writing the packets out as SubRip, which the core parser already reads. No subtitle decoder and no new FFmpeg symbols. Picture-based subtitle formats (DVD, Blu-ray PGS, DVB) stay unlisted — drawing them needs an image path that does not exist. Media Foundation still lists none.
- Consequences: RV-059 is complete. A file with subtitles inside it now shows them without a file beside it, and `C` cycles streams, then files, then off. The read happens on the caller's thread with its own format context, so the decode thread is untouched; a long film pauses for a moment and the OSD says why. RFC-0001 §11's M5 entry and §11.3's 3.16.1 row are amended.
- Supersedes: the "not listed, not shown" half of D-10. D-10's rule itself — do not offer a track nobody can display — is what keeps the picture-based formats out.

## 2026-09-13: D-13 The settings window: its own window, drawn from a document, saved in the INI ∩ TOML subset

- Status: Accepted (owner request 2026-09-13; answers "1a, 2a, 3a, 4a" to the plan's questions)
- Context: The settings "window" was a panel painted over the viewer's canvas, each tab built by C code walking a macro table in `settings.c`, and the files the viewer wrote used bare INI values (`decoder = ffmpeg`) and, for the reading history, file paths as keys — neither of which TOML accepts. The owner asked for a separate window; for the page to be described in an internal, script-like format so one interpreter renders it and reacts to changes; for previews and live data to show at once, in a look that may be plain (lists, tables, fixed-width text); and for settings to be saved in the smallest common subset of INI and TOML.
- Decision:
  - **Format.** Every configuration file the viewer writes (`settings.ini`, `layout.ini`, `history.ini`; `keymap.ini`'s built-in text) is in the lines a TOML 1.0 parser and a plain line-based INI reader both accept with the same meaning: UTF-8 without a BOM; whole-line `#` comments; `[section]` and `key = value` with names of `[a-z0-9_-]`, a key once per section and never above the first section; values `true`/`false`, `-?[0-9]+`, `-?[0-9]+.[0-9]+`, or a double-quoted string with exactly the escapes `\\` and `\"`. Reading stays tolerant of older files (a BOM, `;` comments, bare text, repeated keys, keys above the first section) so none stops loading. `scripts/check-conf-format.py` proves it with `tomllib` and `configparser` on files made by the real writers.
  - **One list.** The settings window's document (`src/core/default_settings_doc.c`) declares every setting — type, range, default, `wired` — where it places it; the C table is gone and `check-settings.py` reads the document (2a). The document stays internal (4a).
  - **The window.** A window owned by the viewer's, laid out on a grid of fixed-width cells by `ui_settings` from the document; changes apply to the viewer at once, the file is written when the window closes, and Revert returns to what the file held when it opened (1a).
  - **Keymap.** The built-in keymap quotes every value and puts its top-level actions under `[ui]`, which the parser reads as the old global context (3a).
- Consequences: §3.22.1's OK / Cancel / Apply bar becomes Revert / Defaults / Close. The General tab's keys moved under `[general]`; the reading history is one `[entry-N]` section per book. A new kind of line, `action`, keeps the shell-registration buttons. RFC-0001 §3.22.1, SPEC §29 and R148 are amended to match.
- Supersedes: the overlay settings panel described in main.c's earlier comment and T048's "drawn inside the viewer's own window".

## 2026-09-14: D-14 Keys are changed in the settings window, beside settings.ini, and a key another action holds is refused

- Status: Accepted (owner 2026-09-14: "①-1(a)" — change keys in the window; refuse a key that is taken and say who has it)
- Context: After D-13 the Keys page only listed the bindings. `keymap.ini` was read from the working folder alone and replaced the built-in bindings wholesale; nothing wrote it. `rubraview_keymap_conflicts` compared bindings within one context, and its test called a one-line keymap "the shipped default" — the real default had a clash nobody saw.
- Decision:
  - **In place.** Each Keys row takes the focus; `Enter` (or a click on the row in focus) waits for a key and adds it to that action, `Esc` leaves it; `Delete` takes the action's last key off. Revert and Defaults cover the keys as they cover the settings.
  - **Refuse, and name the holder.** A key another action already has where the two can meet is not added; the message names that action. Nothing is taken from anyone.
  - **Where two contexts meet** follows the dispatcher (`dispatch_key`): the same context always; `navigation`, the global section and `view` among themselves, because they are asked in turn and a key in two of them only ever reaches the first. `slideshow`, `animation` and `subpage` are asked first only while they are active, so a key they share with the rest keeps both meanings (`Space`). `rubraview_keymap_conflicts` uses the same rule, and the Keys page marks a clashing row `! also <action>`.
  - **Where the file lives.** `keymap.ini` is written when the window closes, only if a key changed, next to `settings.ini` — beside the program in portable mode, in AppData otherwise — in the INI ∩ TOML subset (D-13), and read from there; a `keymap.ini` in the working folder still counts when AppData has none.
- Consequences: the Windows key names gained F6–F9, `Semicolon`, `Slash`, `Backquote`, `Quote` and `Numpad0`–`Numpad9` — F7 pressed to rebind used to arrive as nothing. The shipped keymap had one clash, from RFC-0001 itself: `F2` for the toolbox (§3.7.2) and for rename (§3.18.2), and only the toolbox was reached. The owner gave it to rename, as in Explorer (2026-09-15: "f2면 이름바꾸기지요"); the toolbox keeps `T`, and the shipped keymap has no clash. A `keymap.ini` saved before that still holds `T, F2` and shows the clash on the Keys page until its F2 is taken off. §3.22.2's Export / Import is not built.

## 2026-09-15: D-15 The two boxes change with what is on screen, are drawn from a document, and never go below 30 %

- Status: Accepted (owner 2026-09-15, answers to RFC-0002 §8: "Q1-a Q2-a Q3-30% Q4-a Q5-재생버튼과 함께 Q6-미루지 말 것")
- Decision:
  - **Q1 ⓐ** The toolbox profiles and the menu tree are an internal document read by one interpreter, as D-13; a gate checks that every tile and menu item names a handled action.
  - **Q2 ⓐ** Each box has its own opacity: `[ui] menubox_opacity`, `[ui] toolbox_opacity`.
  - **Q3** Alt + wheel over a box steps it 5 % at a time between **30 %** and 100 %.
  - **Q4 ⓐ** Volume and mute are the Windows audio session's (`ISimpleAudioVolume`), persisted as `[audio] volume` and `[audio] mute`.
  - **Q5** The timeline strip ships with the playback tiles, not after them.
  - **Q6** Nothing is deferred: the detached toolbox window, pinning, A-B repeat and playback speed are part of this work.
- Consequences: RFC-0002 is accepted with these answers; its slices S1–S6 and the four Q6 items are delivered in order, each verified before the next.
  - A detached toolbox dropped over the viewer does **not** dock back (RFC-0001 §3.6.1 said it would): over a fullscreen viewer every drop would dock. It docks by its Dock half, `Ctrl+T`, `T`, or the titlebar's box button — the implementer's call, reported as such.

## 2026-09-15: D-16 Keys come from the keymap alone; arrows play, Ctrl+arrows size the window, Alt+arrows move it

- Status: Accepted (owner 2026-09-15, answers to RFC-0003 §7)
- Decision:
  - **Q1** While a video or music page is on screen, **Left / Right seek 5 s** and **Up / Down change the volume by 5 %**.
  - **Q2** **Space** plays and pauses; stop has no key of its own (it is a tile).
  - **Q3** **Ctrl + arrows resize the window** (Left/Right its width, Up/Down its height) and **Alt + arrows move it**, everywhere. The unhandled `pan_*` bindings are replaced by these.
  - **Q4** The `[animation]` context is renamed `[media]`; `[animation]` in a saved `keymap.ini` is still read.
  - **Q5** The key table in RFC-0001 §3.7.2 and the manual is generated from the keymap; rows never built move to a "not built" list.
  - **Q6** Ctrl + Left / Right no longer seek; seeking 30 s is left for later.
- Consequences: `up_to_folder` loses `Ctrl+Up` to window sizing and moves to **`Ctrl+Backspace`** — Backspace already means "back" (a page), and Ctrl widens it to the folder. That key is the implementer's pick, not the owner's, and is reported as such.


## 2026-09-15: D-17 Always on top, one tap away

- Status: Accepted (owner 2026-09-15: "특성상 항상 맨위에 옵션이 필요합니다. 메뉴에 추가 바랍니다. 쉽게 토글할 수 있는 위치여야 해요. 호버링으로 뜨는 타이틀바에 핀 모양 아이콘이나 Pin 텍스트 버튼으로 추가하는 것도 좋습니다.")
- Decision:
  - `[general] always_on_top` (default false) keeps the viewer above other programs' windows; it is on the General page of the settings window and saved.
  - It toggles from a **Pin** button on the hover titlebar (lit while on), from the menu box's **first entry**, "On top: on/off", at the top level, and with the action `toggle_always_on_top`.
- Consequences: the key `Ctrl+Shift+T`, the button's text form ("Pin", not an icon) and the menu placement are the implementer's picks. The boxes document grammar gains a top-level `item`.
