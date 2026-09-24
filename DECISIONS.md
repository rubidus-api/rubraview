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
- Decision: Decouple the Core Engine (pixel math, resampling, convolution, batch queue) from Win32 APIs. The test suite compiles and runs natively on Linux host (`make test`). Windows binaries cross-compile using MinGW-w64 on a Linux build host (`make win64`).
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

## 2026-09-21: D-18 Menu grid of 16, Delete asks first, Order shows its way, the picker lists what opens

- Status: Accepted (owner 2026-09-21, answers to four questions asked in the conversation: grid "한도를 16칸으로 늘림", Delete "누를 때 확인 받기", Order "Order: L>R / R>L", picker "열 수 있는 것만")
- Decision:
  - A menu level holds up to **16 tiles** (4 x 4) instead of 12; the D-15 tree is not split. File and Playback had 11 items + Back.
  - The menu's **Delete asks first**: the first tap arms it (the tile reads `Delete?`, the OSD says to tap again), a second tap on it within 5 s acts; any other tap, or waiting, disarms it. The `Delete` key is unchanged.
  - The reading-order tile reads **`Order: L>R` / `Order: R>L`**, in the menu and in the toolbox.
  - The picker (Open folder) lists **folders and the files the viewer opens** (pictures, video, music, archives); the bottom bar says how many other files were left out. A folder with nothing openable is not entered; the OSD says so.
- Consequences: menu tiles say more about their action (D-15 polish, same day): submenu `>`, switch state, current choice edged, dimmed when useless. `ui_actions.c` holds the rules and the confirm; tested under T028; the picker filter under T030.

## 2026-09-22: D-19 Hardware decode ships behind a setting, off, until a real card has been measured

- Status: Accepted (owner 2026-09-22, "1·2단계 먼저 진행" on the three-step plan: safeguards, zero copy, then T065 on a machine with a GPU)
- Decision: `[video] hardware_decode` = `off` (default) | `on` (hand the renderer's device to Media Foundation where the card offers decoders) | `always` (diagnostic). Frames decoded on the card are copied into the film's texture on the card (zero copy, D-11 (b)); a reader that fails with the device is reopened without it. FFmpeg stays software (its D3D11VA output needs a colour conversion of its own).
- Consequences: the default is revisited after T065 on a real GPU. The texture path already runs on the VM (plan file, 2026-09-22), so a regression there is caught before any real card is at hand.

## 2026-09-25: D-33 Text subtitles in a box the reader moves and sizes; the Sub tile toggles and chooses

- Status: Accepted (owner 2026-09-25: "자막 출력 부분을 떠다니는 반투명 창처럼 하면 좋을 것 같네요. 터치하면 창 외곽선이 굵게 보이고, 그 상태로 오른쪽 상단 위에 S(설정) M(이동) R(리사이즈) X(종료) 버튼이 뜨고, 이동이나 리사이즈는 버튼 위에서 드래그하게요. 툴박스에 sub 버튼이 있어서 그거 누르면 자막이 꺼지고 켜지고, 더블클릭이나 프레스 홀드 하면 여러 자막 중 원하는 언어 자막 열 수 있게 하고요.")
- Decision: text subtitles are drawn in a translucent box inside the viewer's window (not an OS window: it must sit over the film in fullscreen). A tap selects it — thick edge, S M R X above its top-right corner, answering before the rest of the chrome; a tap elsewhere deselects and does nothing else. M dragged moves it inside the window; R dragged holds the bottom-left corner and sets the width and the height, and the height *is* the subtitle size (`video.subtitle_size` is written, so the settings page and the box never disagree). S opens Settings › Video; X turns subtitles off. The box's shade is `video.subtitle_background` (0-100 %, default 35). An empty box draws nothing and takes no taps. Its place is kept in `layout.ini` `[subtitle_box]` as fractions of the window. The toolbox's Sub tile is `toggle_subtitles`: a tap turns subtitles off and back on to the track last shown; a second tap within 0.35 s or a press held 0.5 s opens a list (Off, then each track) above the tile — so this one tile acts on release. The menu's Subtitles gains On/off and Choose; `C` still cycles.
- Consequences (implementer's choices the owner can overrule): DVD and Blu-ray picture subtitles stay where the disc put them (D-22, D-27) and obey on/off; the list is a small popup of its own rather than a submenu. Logic is host-tested in `src/core/subbox.c` (T088).

## 2026-09-25: D-32 proven_c_lib is used for what it already solves; u8str_t stays

- Status: Accepted (owner 2026-09-25: "rubraview의 코드들이 proven c lib 의 규약이나 방식을 쓰고 proven을 적극적으로 활용해서 작성되었는지 다시 한번 잘 검토해서 수정해 주세요. 안정적이고 퍼포먼스를 잘 살릴 수 있게요.")
- Decision: where proven already answers a problem the viewer solved by hand, proven's answer is used — checked size arithmetic (`PROVEN_CKD_MUL`, through `rubraview_arena_alloc_array`), number parsing (`proven_parse_double_ascii`, `proven_scan_i64`, through `number.h`), sorting (`proven_array_sort`, introsort). String comparisons are one set in `core.h` instead of a dozen private copies. Kept on purpose: `u8str_t` stays rubraview's own char-typed view (proven's is `{const unsigned char *ptr; size}`; renaming thousands of `.len` buys nothing at run time) and `rubraview_u8_view()` hands a view to proven; the PCM ring stays (single producer, single consumer, atomics — `proven_ring` has no thread safety); decoder threads keep `malloc` (LESSONS 2026-09-11: a decoding thread never touches the app arena); `snprintf` into fixed UI buffers stays.
- Consequences: three proven sources (`float_parse`, `float_decimal`, `scan`) join the build; on the host they are compiled as objects without `-pedantic` because `float_decimal.c` uses `unsigned __int128`, and the vendored sources are untouched. Every sort order is total now (ties go to the natural name, the name's bytes, then the listing). Found and fixed on the way: `rubraview_ini_get_int` overflowed on a long value, `nan`/`inf` were accepted as settings, two actions read a number past their view, `rubraview_pixbuf_create` multiplied a width in `int32_t` unchecked. Bicubic and Lanczos compute column weights once (byte-identical, 1.4-2.4x faster). Not done: a separable two-pass resampler would be faster still but changes output by rounding.

## 2026-09-24: D-31 The mini player is a window of its own, and drives whichever track is sounding

- Status: Accepted (owner 2026-09-24, RV-081's second half)
- Decision: `Shift+P` opens a small window (320x80 of client, on top, its own renderer and event pump — the F1 help window's shape) showing the cover, the track, who made it, the elapsed time and a strip, with three buttons: back a track, play or pause, on a track. It speaks to the background music when there is any and to the page's own track otherwise, so the reader never has to ask which one a button will move.
- Consequences: the cover is decoded a second time for that window, because a texture belongs to the renderer that draws it. Play or pause through the mini player counts as the listener's own pause (D-30), so it outranks the arbiter. The outer buttons move to the neighbouring *music* page in the folder: with the music on the page the viewer turns to it, and with it in the background the reader stays where they are while the music changes. With nothing playing the window says so rather than leaving the last track's place on the strip.

## 2026-09-24: D-30 Music outlives its page, and an arbiter says who gets the speakers

- Status: Accepted (owner 2026-09-24, RV-081's first half)
- Decision: a track that is still playing is not closed when its page leaves — the same player is handed to a background slot and goes on (§3.14.6). Coming back to its page hands it straight back, at the moment it had reached; nothing is opened twice. When a page with sound of its own is opened the music stands aside, and it comes back when that page ends or goes. The deciding is a state machine in `src/core/music.c` (`rubraview_bgm_event`) and is tested on the host.
- Consequences: **the listener's own pause outranks the arbiter** — music stopped by hand does not come back to life because a film ended, which is what the state machine remembers and what its test pins down. The hand-over lives inside `media_close`, so no path that closes a player can forget it. `Space` with no player on the page is the background music's. `audio.bgm_pause_on_video` is a live setting; with it off the two play together. A track handed to the background does not get gapless or a crossfade — a transition belongs to the page that is being read.

## 2026-09-24: D-29 The next track is opened early, and the two overlap on an equal-power curve

- Status: Accepted (owner 2026-09-24: "E도 진행해 주세요", RV-075 being the gapless and crossfade part of it)
- Decision: while a track plays, the next page is opened as a second player — two seconds before it is wanted, plus the crossfade — and takes over at the end without anything being opened at that moment. The overlap uses `cos`/`sin` of the same quarter turn rather than a straight line, because two sounds at half amplitude are not half as loud together and a linear fade dips audibly in the middle. The deciding is a pure function (`rubraview_track_plan`, `src/core/music.c`) and is tested on the host; the viewer only carries it out.
- Consequences: two players are alive at once, which needs a level per player — `rubraview_pal_media_set_gain`, applied where the WASAPI output fills the device buffer. The process-wide volume of D-15 is untouched. "The next track" is the next *page*, and only when it is a music file: a picture or a film is never slid into. A crossfade is never more than half of the track it is leaving, so a short one is still heard by itself. A file that does not say how long it is gets no transition at all, since one can only be timed against a known end. `audio.gapless` and `audio.crossfade_seconds` are now live settings.

## 2026-09-24: D-28 The A-B points are named by key and by number, and the two cross over

- Status: Accepted (owner 2026-09-24, relayed: "A B 반복을 수동으로 시간을 입력할 수 있으면 좋겠네요 ... 서로 교차해서 지정할 수 있게요")
- Decision: `[` and `]` keep setting A and B where the playhead is. `Shift+\` opens a box holding both points as text in the timeline's own spelling, and inside it `[` and `]` stamp the playhead into the field in hand while the keyboard types over it. `Enter` keeps what is written, `Esc` leaves the points as they were, `\` empties both fields, `Tab` swaps. So neither way of naming a moment is the primary one: a tapped point can be corrected to the millisecond, and a typed point can be re-tapped.
- Consequences: reading a time is core and tested (`rubraview_parse_timecode`, `test_playback`) — it accepts `83`, `1:23`, `1:23.5` and `1:02:03.500`, and refuses anything else rather than half-reading it, because a field is either a time or it is not. A field left empty unsets that point. While the box is open every other key is swallowed, the way the rename box swallows them. The box takes only digits, `:`, `.` and `,`.

## 2026-09-24: D-27 Blu-ray picture subtitles are read here too

- Status: Accepted (owner 2026-09-24: "D 구현 바랍니다", D being Blu-ray `.sup` subtitles)
- Decision: PGS (`movie.sup` beside the film) is supported the way VobSub is (D-22): `src/core/pgs.c` walks the segments, notes when each display set is shown and where it begins, and decodes one picture at a time — the palette from Y'CbCr, the picture from its run-length coding. No FFmpeg and no decoder, so it works with the Windows backend alone. A `.sup` appears among the subtitle tracks (its kind reads `sup`) and is chosen like any other; `Z`/`X` move it in time like text.
- Consequences: only the index of times and offsets is held, because a feature film's subtitles are hundreds of megabytes of pictures. A display set that only sends a new palette is a step of a fade and neither starts nor ends a subtitle. A set that composes two objects is drawn as its first one, and a composition that refers to a picture an earlier set sent is not drawn — both are written down in T081 rather than guessed at. A damaged subtitle costs that subtitle, not the file.

## 2026-09-24: D-26 FFmpeg is pinned to a release, and that release is 9.0

- Status: Accepted (owner 2026-09-24, after asking what to use: "9.0으로 올리고 0.0.17로 릴리즈 바랍니다"; the question before it was whether master would be better)
- Decision: the vendored headers follow the newest FFmpeg **release** — 9.0.2 today — and never `master`. The viewer reads FFmpeg's structures by the layout its headers describe and only checks the major version at run time, so a build whose major matches but whose layout has moved would be read wrongly. A release branch does not move that way; master does, daily.
- Consequences: the DLLs to fetch are now `avcodec-63`, `avformat-63`, `avutil-61`, `swscale-10`, `swresample-7`, and every place that names them was changed together (both readmes, the settings window's help, the vendored notes). A reader who already had 8.1's DLLs has to fetch 9.0's; the old ones are refused rather than misread. The next major will be the same small piece of work: swap the headers, change the names in those four places, measure on the VM.

## 2026-09-23: D-25 GPU decoding is on by default, and a file's languages are separate tracks

- Status: Accepted (owner 2026-09-23: "기본값 on 으로 바꾸고 릴리즈 바랍니다", and "smi나 srt 등의 다른 자막들도 멀티 언어인 경우가 있습니다 ... 선택한 언어 자막만 잘 보여줄 수 있게")
- Decision:
  - `[video] hardware_decode` ships `on`, which D-19 left to be decided once a real card had been measured. T065 measured two: AMD Radeon 196 pictures a second at 4K and Intel UHD 730 43, against 16-21 in software, the picture right on both. `always` stays as a diagnostic and the settings window now says so in as many words ("for testing only").
  - A subtitle file that holds several languages gives one track per language, as a DVD index already does. For SAMI that is its classes; the track is named after the class, and only that language's captions are shown.
- Consequences: the SAMI reader was wrong before this, not merely limited — it ended every caption at the next caption of any language, so a two-language file showed nothing at all. `rubraview_subtitle_languages` and the track's `shown_language` are the core of it, covered by `test_subtitle`.

## 2026-09-23: D-24 The help is a window of its own, and the keys in it are generated

- Status: Accepted (owner 2026-09-23: "모달리스 창으로 F1 도움말 넣어주세요 도움말의 가장 중요한건 단축키입니다", and "readme 에 단축키맵 항목 ... 한국어판 영어판 두개")
- Decision:
  - `F1` opens a help window that belongs to the viewer but does not stop it: the reader can try a key while the list is on screen. `F1` or `Esc` closes it, the wheel and the paging keys scroll it.
  - What it lists is built at run time from the keymap in force (`src/core/help.c`), not written by hand, so a key rebound on the settings window's Keys page shows its new binding. An action is named the way the menu or the toolbox names it; one with no such name is spelled out from its id.
  - `F1` stops being a second key for the menu box, which keeps `Tab`.
  - The same table goes into `README.md` and `README.ko.md` under "Keyboard shortcuts", generated by `scripts/check-actions.py --write` from the same keymap, with Korean wording kept beside the English in that script.
- Consequences: the help has no prose beyond two lines — the keys are the help (owner). `check-actions.py` now fails when a bound action has no description, in either language, which keeps the tables honest.

## 2026-09-23: D-23 Picking several files by tapping, and renaming the whole name

- Status: Accepted (owner 2026-09-23, six requests and four answers: modes stay on until turned off, a new extension is typed, the picked files can be renamed by extension / opened / recycled / moved / copied / sent to a new playlist, and changing an extension is not asked about)
- Decision:
  - The picker has three modes (`rubraview_picker_mode_t`): a tap opens, a tap picks one, or two taps invert a range. A mode stays on until its button is pressed again, because the viewer is used over Remote Desktop where holding Ctrl or Shift is awkward. A range inverts — what was picked inside it is unpicked.
  - Folders, `..` included, are opened, never picked.
  - `..` is the picker's first entry wherever there is a parent, so going up needs no keyboard.
  - The rename box holds the whole name; the extension may be changed and nothing is asked. The box draws a blinking caret.
  - One typed extension can be given to every picked file at once, each rename its own undo entry.
  - A picked set can be sent to a new playlist (`PlaylistNNNN.m3u8` beside the files, then opened — the owner preferred this to opening them loose), to the recycle bin (a second press confirms), or moved or copied to one of the numbered folders, chosen by pressing 1-9 after the button. Each file is its own undo entry.
- Consequences: the action bar is two rows and nine buttons. The picker now draws the OSD and the text box itself, having covered both. Two faults surfaced while measuring: the config files were found by name alone rather than beside the executable (so the numbered folders never worked at all), and a number key reached §3.18.3 while the picker was open.

## 2026-09-23: D-22 DVD picture subtitles are read here, not by a decoder

- Status: Accepted (owner 2026-09-23: "dvd그림자막도 지원하게 해 주세요", after asking which subtitle formats work)
- Decision: VobSub (`movie.idx` + `movie.sub`) is supported: the index gives the palette, the frame size, the languages and each subtitle's time and position in the `.sub`; the subpicture is demuxed out of the MPEG program stream and its run-length picture decoded in `src/core/vobsub.c` — no FFmpeg, no decoder, so it works with the Windows backend alone. A `.idx` appears among the subtitle tracks and is chosen like any other.
- Consequences: only the index is held; a picture is decoded when it is shown and uploaded as one small texture, because a feature film's subpictures would be hundreds of megabytes at once. The first language in the index is used (choosing among the languages inside one `.idx` is backlog). Blu-ray `.sup` (PGS) is a different format and is not read. A subtitle whose bytes are damaged is skipped, not the file.

## 2026-09-23: D-21 A film starts on the file's first sound track

- Status: Accepted (owner 2026-09-23, on the open question of which sound track opens when no language is preferred: "그냥 트랙 순서로 한다면?")
- Decision: with no preference, the sound track the file lists first plays, and it is #1 in the track list. The system's language does not count.
- Consequences: Media Foundation does not expose the file's order (its MP4 source numbered two_audio.mp4 kor, eng, video, identifiers included), so the viewer takes the stream Media Foundation selects by itself — which is the file's first sound track — and lists it first; the others follow in the reader's order. FFmpeg takes the first decodable sound stream in file order instead of `av_find_best_stream`.

## 2026-09-22: D-20 The toolbox becomes a strip; both boxes get a pin

- Status: Accepted (owner 2026-09-22: "박스의 왼쪽은 드래그로 ... 클릭하여 그 내부 박스를 열고 고정 ... 오른쪽은 호버링만 해도 창이 뜨는", "도구 박스는 가로로 긴 타입 ... 맨 위에 재생바 그 밑에 파일명 그 밑에 버튼들 ... 메뉴 버튼의 절반 길이, 면적은 1/4 ... 아이콘으로", "두 박스에는 각각 pin 기능 ... 왼쪽위에 핀 모양 아이콘이 작게", "ffmpeg를 어떻게 넣어야 하는지 ... 도움말을 설정화면에")
- Decision:
  - Anchor: the left half drags, and a click opens and pins; the right half opens on hover and nothing more. An opening box stays inside the window.
  - The toolbox is a strip (RFC-0002 §6.3): pin + seek bar, the file's name (or the hovered button's caption), then 32 px icon buttons, eight a row. Video and music start previous, next, −5 s, +5 s, play/pause, stop. Up to 24 buttons a profile.
  - Each box has a pin; pinned, it stays open when the pointer leaves. Amended the same day (owner: "팝업창이 뜰 때만 원래의 2x1 크기의 플로팅 박스 왼쪽 또는 오른쪽에 핀 아이콘이 생기는 쪽이 더 좋은 것 같아요. 팝업창이 사라지면 핀 아이콘도 사라지고"): the pin is not a row in the box but a third anchor-sized square beside the anchor — right of it, left when the window has no room — shown only while the box is open. The menu box has no header row; the toolbox's seek bar takes its top row alone.
  - Settings › Video ends with "Adding FFmpeg (optional)": DLLs, not the exe; the five names; beside rubraview.exe; version 8.1; BtbN's `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`. The settings document gains `note "text"`, a full-width help line.
- Consequences: "창 밖으로 나가면 ... 이전 위치로 복원" is read as "the pointer leaving an unpinned box folds it back to its anchor" (the implementer's reading). The icons are Segoe MDL2 Assets, with the old short captions where the font is missing; the icon choices, the eight-per-row width and the 9.x warning are the implementer's picks. BtbN's newest builds are 9.0 and master, which this build refuses, hence the exact file name.
