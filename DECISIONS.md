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

## 2026-09-30: D-60 Folders typed in the settings window; favourites renamed and moved

- Status: Accepted (owner 2026-09-30: "경로 직접 입력 가능하게. 반대로 현재 경로를 편집 가능한 에디트 컨트롤에 넣어서 복사해갈 수도 있게. 즐겨찾기 이름 바꾸기와 순서 바꾸기도 구현.")
- Decision:
  - A path setting (the numbered folders) opens, on `Enter` or a click, a text box over its row with the path in it, all of it selected: typing replaces it, the editing keys and `Ctrl+A/C/X/V` work as in the rename box (the key code is now one `textbox_key` for both), `Ctrl+O` browses into the box, `Enter` keeps a folder that exists (quotes from Explorer's "Copy as path" are taken off; empty clears), `Esc` or a click elsewhere leaves the setting alone. The picker's path box already did this for the folder on screen (D-42).
  - Favourites carry an optional `name` in their `favorites.ini` section (`rubraview_favorites_title/rename/move`): a right-click on a favourite chip opens the rename box with its title; a left press opens it on release, or, dragged 6 px or more, moves it where it is let go, with a bar showing where.
- Consequences: measured on the Windows 11 VM: a favourite dragged from the third place to the first was written first; a right-click, `tunes`, `Enter` wrote `name = "tunes"` and the chip read `tunes`; in Settings › Files, Folder 1's path copied with `Ctrl+C`, pasted into Folder 2, edited to `rbinfo` and kept; a path that is not a folder said so and kept the box open. The VM's own `settings.ini` and `favorites.ini` were put back afterwards. Not measured: `Ctrl+O` in the box, typing Hangul there.

## 2026-09-30: D-59 A card that refuses a picture is simulated to run the halving

- Status: Accepted (owner 2026-09-30: "백로그 있나요. 안한 것 있으면 진행 바람." — the halving had been left open since D-39 because no machine here refuses a bitmap)
- Decision: `RUBRAVIEW_REFUSE_BITMAP_PIXELS=N` in the environment makes `pal_image_wic.c` treat any bitmap over N pixels as refused (`E_OUTOFMEMORY`, what a card short of memory returns), so the fall-back that halves a picture until the card takes it runs for real on the test VM. Unset, nothing changes; the variable is read once.
- Consequences: measured on the Windows 11 VM with N = 4 000 000: `a_photo.jpg` (4032 x 3024, 12.2 megapixels) was refused at full size and shown at 2016 x 1512, the information window saying "reduced to 2016 x 1512 (the graphics card's limit)". The variable was removed from the VM afterwards. What is still unmeasured is a real card refusing, which only the owner's machines can show.

## 2026-09-29: D-58 A settled screen is drawn when it changes, not every second

- Status: Accepted (owner 2026-09-29: "백로그 있나요. 안한 것 있으면 진행 바람." — found while measuring on the VM)
- Decision: after the 3 s grace that follows any input or activity, the main loop no longer draws a frame each second. It sums up what is on screen (`scene_signature`: window size, the spread and its pages' loaded state and texture, zoom and pan, the boxes', title bar's, OSD's, notice's, playlist window's and picker's state, the media page, its pause and its time to the second, the far-page reader) each pass and draws when that changes, and every 10 s regardless, as a safety net for anything the summary misses.
- Consequences: measured on the Windows 11 VM (WARP, no graphics card), a 4032x3024 photo at rest: 10.3 s of CPU in 30 s before, 1.38 s after; 0.0.17 had 4.1 s and 0.0.19 18.9 s. A page turn and a hover drew at once, as before. Something visible that changes without input and is not in the summary shows up to 10 s late; the list is the place to add it. Music playing under a picture costs nothing extra: it is held apart from the page (`bgm_media`), and only the mini player, a window of its own, shows its time (checked 2026-09-30; an earlier note here said otherwise).

## 2026-09-29: D-57 What happens when a film or a song ends

- Status: Accepted (owner 2026-09-29: "동영상/음악 재생이 끝나면 어떻게 할 것인가 옵션 추가. 정지 / 다음 파일 재생 선택지. 그리고 루프 종류도 다양하게. 현재 파일 재생 후 정지 / 현재 파일만 루프 / 현재 폴더 또는 재생 목록 전체 루프 / 셔플(하지만 한번씩은 거치게 ...) 루프는 툴바에 버튼이 있어야 겠네요.")
- Decision:
  - One setting, `audio.at_end` = `stop | next | one | all | shuffle` (default `next`), holds both of the owner's lists: "정지" and "현재 파일 재생 후 정지" are `stop`, "다음 파일 재생" is `next`. A toolbox button in the video and music profiles cycles it (captions Once / Next, icons RepeatOne / RepeatAll / Shuffle), as do `Ctrl+R` and Playback › At the end.
  - Only films and songs take part; pictures between them are passed over (`src/core/repeat.c`). `next` stops after the last, `all` goes round to the first, `one` and a lone file play again from the start. The page chosen for a file is decided once, so gapless and crossfaded music prepares the same track the end goes to; a slide show still decides for itself, and A-B repeat never reaches the end.
  - Shuffle: one flag a page (played in this round) and the page that played last. A round ends when every playing page has its flag; the flags clear and the next pick still avoids the last one, so none plays twice in a row. The owner's question — n or n+1 — is answered as n flags plus one index: a list of recently played files needs room for n (a round), and one more to carry the last across the round's end. A list of another length (files added or removed) starts a new shuffle.
- Consequences: a film that ends now goes on to the next film or song (it used to hold its last frame); `Once` gives that back. Measured on the Windows 11 VM with `rbmusic` (two 6 s WAVs), reading the window title each second: next — 01, 02, then 02 held at 00:06 paused; one — 01 from 00:05.4 back to 00:00.3, three times; all — 02, 01, 02, 01; shuffle — 01, 02, 01, 02; the toolbox button showed the shuffle icon. The title bar's Size (D-56) measured too: dragged 200 px left, the window narrowed by about 190 px from its right edge.

## 2026-09-29: D-56 A fuller, resizable toolbox; edge buttons; the playlist window; I for information; the title bar's Size; the icon

- Status: Accepted (owner 2026-09-29: "툴바에 대부분의 기능을 넣고, 툴바가 확장된 다음에 핀 아이콘 오른쪽에 크기조절 아이콘을 넣어서 ... 메뉴바도 드래그 가능하게 ... 창의 왼쪽에 세로가운데 그리고 오른쪽의 세로가운데에 ... 이전/10개전/맨처음 ... 이전5초/이전30초/이전파일 ... 재생목록(재생목록이 없으면 현재 폴더 파일들) 버튼과 재생목록 창 ... 반투명 플로팅 ... I 누르면 exif 정보나 해상도, 파일 크기, 생성일 수정일, GPS정보 ... 타이틀바 더블클릭하면 최대화/창크기 교대로 ... 핀과 최소화 버튼 사이에 Resize 버튼 ... #9b1b30 배경색 사각형에 검정색 선으로 눈이 그려진 아이콘")
- Decision:
  - Toolbox profiles hold 21 to 27 buttons (up to 40): pages one, ten and all the way, zoom and fits, turns and flips, slides, filmstrip, playlist, information, edit, export, rename, open, on top, settings, full screen; films add 30 s steps, A-B editing and the tracks. Delete stays in the menu, where it asks first.
  - Each open box has a grip, a square past its pin (`rubraview_box_grip_*`): sideways changes how many a row (toolbox 2–24, menu 1–8), away from the box or back changes their size (75–200 %, 5 % steps); "away" is the way the body grows. Kept as `ui.toolbox_columns/size`, `ui.menubox_columns/size`, also on Settings › Display. "메뉴바도 드래그 가능하게" is read as the menu box getting the same grip: it already moved by its anchor's left half (D-20).
  - Edge buttons (`ui_edgenav`): three a side at the vertical middle, shown with the pointer within 96 px of that side and near the middle, not over a box, the playlist window, the picker, settings or a panel, and gone when the pointer leaves the window (`WM_MOUSELEAVE`, a new window event). The right side's "10개전/맨처음" is read as ten on / last, mirroring the left. `media_seek_forward_long` / `_back_long` (30 s, `Shift` + arrows in the media context) are new actions.
  - The playlist window (`ui_listwin`, `P`, a toolbox button, File › Playlist): in the viewer's own window, see-through (three quarters of the toolbox's opacity), at the right; titled Playlist for a set of files, else In this folder / In this archive, with the position; the file on screen marked and followed.
  - `I` opens the information window (was the status line, now `Shift+I`); `Ctrl+I` still does. The EXIF reader adds time zone, lens maker, serial, description, digitised time, program, metering, white balance, exposure mode, scene, digital zoom, colour space, and from GPS the time, direction and speed; decimal coordinates are shown beside degrees and minutes.
  - Title bar: `Size` between the pin and minimise sizes the window from its bottom-right (`SC_SIZE`); a double press on the caption (0.5 s, 4 px) maximises or restores. Moving and sizing now wait for the pointer to move with the button held: started on the press, the system's loop swallowed the second click.
  - The icon is drawn by `scripts/make-icon.py` (16–256 px, a solid iris below 24 px), compiled in with `src/app/rubraview.rc` (windres), which also carries VERSIONINFO from `version.h`; the window class uses it.
- Consequences: measured on the Windows 11 VM the same day: the taskbar and the information window showed the eye; the left stack read `<`, `-10`, `|<` and the right one's buttons turned 1/6 to 2/6 and to 6/6, the left bottom back to 1/6; over a film the right stack read `+5s`, `+30s`, `>|`; `P` listed the folder's six files with `2 / 6` after a click on the second; `I` on `rbinfo\exif_photo.jpg` showed `Coordinates 37.566500, 126.978000`; the toolbox grip gave `10 a row, 150%` and the menu's `6 a row, 125%`, kept after release; a double click on the title bar of a smaller window filled the work area. A window resized from outside by `SetWindowPos` without activation got no pointer moves on the VM, in the release before it as well — a test-harness effect, not changed here.

## 2026-09-29: D-55 The download is rubraview.exe, in a zip and on its own

- Status: Accepted (owner 2026-09-29: "파일 배포판 패키징 할때, 압축파일 내 exe 파일에선 버젼 정보를 지우고, mfprove 파일은 이제 필요없지 않나요. 불필요한 파일은 제거하고, 그리고 zip 판 외에도 exe만 올려서 바로 받을 수 있게 하는 것도 좋을 듯 해요")
- Decision:
  - The exe in the release zip is `rubraview.exe`; the version stays in the zip's and the folder's name (`rubraview-v<version>`) and in the program's title.
  - The zip holds the program, the manual, CHANGELOG, LICENSE, THIRD_PARTY_NOTICES and `licences/` (now with proven_c_lib's, which was missing). `rubraview-mfprobe.exe`, `gpu-check.cmd`, `VERSION` and the import list leave the zip; `make mfprobe` and `tools/gpu-check.cmd` stay for developers, and the import list is written beside the zip.
  - The release also carries `rubraview.exe` alone. Because a lone exe comes without its licence files, every notice is compiled into the program (`scripts/gen-notices.py` → `src/core/notices_text.c`, checked by `project-check.sh`) and shown at the end of the F1 help.
  - `README` links `releases/latest/download/rubraview.exe`.
- Consequences: the zip and the lone exe are the same bytes. Measured on the Windows 11 VM: the lone exe opened a picture titled `a_photo.jpg (1/6)` with its version, and F1 then End showed the libjpeg-turbo notice last. The exe grew by the notice text.

## 2026-09-29: D-54 The seek bar is the toolbox's only; nothing the toolbox says is said again

- Status: Accepted (owner 2026-09-29: "현재 동영상 재생할 때, 툴바와 화면 아래에 동시에 재생막대가 뜹니다. 툴바에만 뜨게 해 주시고 나머지도 마찬가지로요.")
- Decision:
  - The timeline strip along the bottom (RFC-0002 §4.2) is gone, and with it its click and drag; the toolbox's seek bar (and page bar, D-44/D-46) is the one. What only the strip showed moves onto the toolbox's line under its bar, at the right as the page count is: the time and the length, the volume (or "muted"), the speed when it is not 1x, and "A-B" / "A-" while a repeat is set (`rubraview_media_status`).
  - The status line (`I`) no longer repeats the name and the position, which the toolbox's line carries; it keeps the picture's size, the zoom and "shown reduced" (`rubraview_osd_format` with no name and no count).
  - The subtitle box still sits, unplaced, above where the strip was.
- Consequences: with the toolbox closed, the film's time is not on screen (the window's title still carries it). Measured on the Windows 11 VM: a paused film showed no strip at the bottom; the toolbox read `e_clip.mp4   00:10 / 00:10   vol 100%`, the status line `640 x 360 | 100%`.

## 2026-09-29: D-53 Help text copied, covers for large archives, a folder's summary

- Status: Accepted (owner 2026-09-29: "세개 다 추가." — the three offered after D-52)
- Decision:
  - The F1 help window's text is marked with a drag and copied (`Ctrl+C`, `Ctrl+A`), as the settings and information windows are; its rows are laid out on the cells they are drawn on.
  - An archive's cover in the picker is read through a mapping (its index where it lies, the first page's bytes only), streaming a solid 7z block past 24 MB; a first page with more than 64 MB decoded before it is left without a cover. This replaces D-35's timed reads into a buffer committed at the archive's whole size, which a multi-GB book never finished within 1.5 s or could not even commit.
  - The information window ends with "This folder": pictures, films, songs, archives (each with its size), subfolders, other files, and all files — listed once per folder in memory of its own. For a book: its folders inside and its pages' unpacked size.
- Consequences: measured on the Windows 11 VM: a drag across a help row copied `Ctrl+I                          Information`; `big.cb7` (1.25 GB, one block) and `big.cbz` (5 GB) got covers where they had none; `rbtest`'s summary counted 5 pictures, 1 film and 1 folder, as PowerShell does.

## 2026-09-29: D-52 Inside a book: thumbnails, entering one unread, and a drive that goes away

- Status: Accepted (owner 2026-09-29: "다음작업들 추천 순서대로 진행해 주세요" — the backlog left by D-47, in the order recommended)
- Decision:
  - Pages inside a book get thumbnails in the picker: the main thread reads the page (three a pass, never a page far inside a streamed 7z block) and hands a copy of its bytes to the thumbnail thread, which decodes it small. The queue owns those bytes and frees them however a request goes (`rubraview_thumbq_push_bytes`).
  - A right-click on an archive in the picker, or Shift+Enter, opens the book and keeps the picker at its top, to choose where to start; a plain tap still opens it for reading.
  - A mapped file whose drive goes away (a network drive, a card pulled out) no longer takes the viewer down: a vectored exception handler watching only the mapped ranges swaps a view that raised an in-page error for reserved memory at the same addresses and commits a zero page wherever it is read; the page fails and the viewer says "the archive is no longer there".
- Consequences: measured on the Windows 11 VM: thumbnails appeared for `big.cbz`'s pages; a right-click on `big.cb7` showed its Vol 1-3 without reading. The drive going away could not be made to happen there: a loopback share removed under an open archive (sessions closed) did not stop reads — the build before this survived it as well — so the guard is built and harmless but not measured.

## 2026-09-29: D-51 The file's information in a window of its own, its text marked and copied

- Status: Accepted (owner 2026-09-29: "지금 재생중인 파일의 용량/픽셀단위크기/코덱/EXIF/기타 자세한 정보를 보여주는 기능도 만들어 주세요. 있으면 잘 다듬어 주시고요. 나와있는 정보는 드래그해서 텍스트로 원하는 부분만 복사해 갈 수있어야 해요." — there was none; the `I` status line was all)
- Decision:
  - `Ctrl+I`, or File › Information, opens a window beside the viewer: the file (name, folder, position, size in bytes and units, modified, created) or the archive page (its entry, stored or deflated, and the archive: kind, size, pages); a picture (format, pixels and megapixels, pixel format, dpi, frames, colour profile, "reduced to … on screen") and its EXIF (camera, lens, taken, exposure, aperture, ISO, focal length and its 35 mm figure, exposure bias, flash, orientation in words, location in degrees-minutes-seconds, altitude, software, artist, copyright); a film or a song (length, picture size and frame rate, picture codec, decoded on the card or in software, sample rate and channels, bitrate — the file's when it says, else size over length —, the backend, every track with its codec, channels, language and title, and a song's tags).
  - It follows the page on screen, a page arriving late included.
  - Its text is a grid of rows, the values in a column of their own: a drag marks a stream of it, `Ctrl+C` copies what is marked (everything when nothing is), `Ctrl+A` marks all — the settings window's selection (D-45).
  - EXIF is read by the viewer itself (`rubraview_exif_read`: JPEG APP1, TIFF, PNG `eXIf`, WebP `EXIF`, "Exif\0\0" near the start for HEIF), every offset checked, a sub-IFD followed one level and never back; the picture's format by WIC without decoding it.
- Consequences: measured on the Windows 11 VM: a photo with EXIF written by Pillow showed every tag as written; a drag across its location copied exactly `37°33'59.4"N 126°58'40.8"E`; a film, a CBZ page of the 5 GB archive and a WAV each showed their sections; turning a page updated the window. Media Foundation's own bitrate is not read (its attribute GUIDs are not in MinGW's headers), so for it the bitrate is the size over the length, and says so.

## 2026-09-29: D-50 What filled the working arena has memory of its own; a low arena goes on in a new block

- Status: Accepted (owner 2026-09-29: "다음작업들 추천 순서대로 진행해 주세요" — the first recommended: the 64 MB working arena that is never reset)
- Decision:
  - VobSub `.sub` and PGS `.sup` files are mapped (like archives, D-47) and let go with the film; the cap goes from 192 MB to 1 GB. Read into the working arena, anything over about 60 MB could never load.
  - The picker builds each listing in one of two arenas of its own (32 MB each), taking turns: the idle one is emptied and filled, and becomes the shown one only when the change succeeds. What outlives a listing is copied out: undo entries, a favourite, a path being opened.
  - As a last guard, a working arena with under 16 MB left goes on in a new 64 MB block; the full one is kept (what is in it is in use). The renderer's texture structs are the heap's, recycled as before.
- Consequences: measured on the Windows 11 VM: an 84 MB `.sup` beside a 10 s film — paused at 5.08 s, the build before shows no subtitle, this one the second subtitle's yellow `(255, 240, 0)` (T081's colour). A 3 000-file folder entered and left 60 times: the build before lost its `..` tile (the working arena was full and the allocation failed without a word); this one's listings stayed whole. Pages (D-49) and archives (D-47) already had memory of their own.

## 2026-09-29: D-49 A far page in a streamed 7z block is read on a thread of its own; archive pages no longer fill the working arena

- Status: Accepted (owner 2026-09-29: "추천대로 진행 바랍니다" on the plan `docs/plans/archive/2026-09-29-far-jump-off-main.md`: the previous page stays on screen with how far the reading has got)
- Decision:
  - A page that needs more than 32 MB decoded first (`rubraview_page_source_read_cost`) is read by a reader thread; the page before it stays on screen with "reading page N… P %". The source is read under one lock; a new jump, or closing the book, calls the read off between 1 MB chunks (`rubraview_sz_cancel`) and waits for it, and the stream stays where it got to. The look-ahead, the GIF check, the tiles copy and the edit preview never start a far read.
  - Every archive page is read into memory of its own, freed once it is a texture (`page_read_owned`). It went into the 64 MB working arena, which is never given back: in a CB7 of 2.7 MB pages, every page from the 16th on came up blank (a compressed CBZ fails the same way, later).
- Consequences: measured on the Windows 11 VM with `rblarge\big.cb7` (1.25 GB, one block): the build before this was blank from page 16 on; now pages 1-36 all drawn. `End`: the page before stayed on screen with "reading page 450… 99 %", then page 450. `End` then `Home` at once: page 1 in 69 ms, the window answering, 154 MB of the viewer's own memory. A far page in such a book gets no GIF frame check (it would be read twice).

## 2026-09-29: D-48 Pages are scaled with Direct2D's high-quality cubic; Scaling filter is read

- Status: Accepted (owner 2026-09-29: "확대축소 품질개선")
- Decision: every page was drawn with Direct2D's plain cubic whatever Settings › Viewer › Scaling filter said (it was shown, never read). Plain cubic takes a small neighbourhood and no more, so a page shrunk to fit keeps a fraction of its detail and aliases. The setting is now read: `bicubic` (the default) and `lanczos3` draw with `D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC`, which filters before it shrinks and is the same cubic when it enlarges; `bilinear` and `nearest` do what they say; the pixel-art toggle still wins.
- Consequences: measured on the Windows 11 VM with a 4000x5600 test page (a fan of 2-px lines, 6-px screentone, circles, text) at fit (0.134x), against an exact Lanczos-3 reduction of the same page: plain cubic 10.1 dB (the lines broke into dots, the screentone into a false pattern), high-quality cubic 23.8 dB, by eye indistinguishable from the reference. `lanczos3` has no path of its own: at this measurement it would not be visibly better; if a difference is found later, a settled-view resample (D-38's Lanczos) is the way.

## 2026-09-29: D-47 Comics of several gigabytes: mapped, ZIP64, streamed solid blocks, a picker inside the book

- Status: Accepted (owner 2026-09-28: "cbz cb7 같은 압축파일 내 파일들을 만화처럼 볼 때 빠르게 파일탐색하고 할 수 있게 해줘요. 파일과 경로가 복잡한 수기가짜리 만화책을 봐도 잘 동작하게요", then "위의 추천한 작업들도 같이 해 줘요" on the plan `docs/plans/archive/2026-09-28-large-archives.md` and its recommended answers)
- Decision:
  - An archive is mapped read-only (`rubraview_pal_fs_map`), not read into the 64 MB working arena: before, a CBZ over about 60 MB did not open, each one opened kept its bytes, and nothing over 2 GB was read (`ftell`). The mapping is let go with the source (`source_close`); the file stays free to rename and delete.
  - ZIP64 is read: the end record and its locator, the `0x0001` extra field, 64-bit sizes and offsets — in the thumbnail thread's tail-only lookup too.
  - A solid 7z block over the page-cache budget (512 MB) is streamed instead of refused: one LZMA/LZMA2/stored decoder kept where the reader is, so turning forwards goes on from there and going back starts again; its window is capped at 1 GB. A far jump in such a block takes as long as decoding up to it (accepted by the owner).
  - Pages inside an archive are ordered folder by folder, each part natural, a level's loose pages before its subfolders (so a cover at the top stays first; Explorer would list the folders first).
  - With a book open, `O` opens the picker inside it at the page's own folder, on the page; its path reads `…/Big.cbz/제 1 권`, `..` climbs through the book's folders and out to the real folder. A tapped page is gone to; the bottom bar's file actions refuse inside a book; no thumbnails there yet.
- Consequences: measured on the Windows 11 VM: a 5.05 GB stored ZIP64 CBZ (1 821 pages in Hangul-named nested folders) opened at once, three page turns within 5 ms, the last page in 91 ms, the process's own memory 194 MB; the picker went to `제 10 권 › Ch 01 › p005.jpg` and the viewer showed page 1206 of 1821, as counted. A 1.25 GB single-block LZMA2 CB7 (450 pages) opened at once and turned forwards at once; the last page took about 8-12 s, during which the window does not answer. The build before this opened neither. Left for later (BACKLOGS): the far jump off the main thread, thumbnails inside a book, entering an archive that is not open, the working arena that other readers (VobSub, PGS) still fill and that is never reset.

## 2026-09-28: D-46 Sort by's orders named for what they do; the picker follows them; the page bar drags

- Status: Accepted (owner 2026-09-28: "정렬에 그냥 문자열 순, 윈도 탐색기식 파일이름 순(숫자는 묶어서), modification time, 파일크기 순서 추가해 줘", then "위에 제안한 것도 이어서 해줘" on dragging the page bar)
- Decision:
  - Settings › Files › Sort by offers, in this order: `text` (the name as plain text), `explorer` (Explorer's order, digits grouped: 2 before 10; the default), `modified`, `size`, `created`. These are the orders that were there as `lexical`, `natural`, `date`, `size`; a settings.ini holding an old word is read as its new one and written back with it (`CHOICE_RENAMES` in settings.c).
  - The picker (`O`) lists in the same order, folders first, instead of always by name.
  - The toolbox's page bar (D-44) can be held and dragged: the page under the pointer is shown, at most ten a second, and the one under it on release; in the detached toolbox window too.
  - Held, it stays a page bar over every page, a film or a song included; where the drag or the click ends decides the bar: on a film or a song it is that page's own seek bar again (owner, same day: "드래그가 끝난 자리나 버튼이 떼어진 자리가 동영상이거나 음악이면 각자 자기 타입의 동작으로"). Measured on the VM: a drag ended on `e_clip.mp4` (5 of 6) — the film played and the bar showed its time.
- Consequences: a choice word renamed later needs its own line in `CHOICE_RENAMES`. Measured on the Windows 11 VM the same day: a stored `natural` showed as `explorer` and was written back as `explorer`; the picker by size listed `rbtest` in the order of the file sizes; a drag along the bar turned 1 → 3 → 4 → 6 and stopped at 6 of 6.

## 2026-09-28: D-45 Sort by is read; the settings window's text can be marked and copied

- Status: Accepted (owner 2026-09-28: "정렬방식은 설정에서 바꿀수 있고. 그리고 설정의 텍스트는 블럭설정 또는 복사 가능하게 해줘")
- Decision:
  - Settings › Files › Sort by and Ascending order a folder's pages and a picked set (they were shown but not read; now `wired`). A fifth choice, `created`, goes last so the file's old values keep their meaning. Changing either opens the folder on screen again in the new order, at the same picture. An archive keeps its natural order.
  - The settings window's text is marked as a terminal marks it: a drag from a line that is only text (a heading, a note, live information, the title or message line), or Shift and a drag anywhere on the page — a plain click on a setting still changes it. `Ctrl+A` marks the page; `Ctrl+C` copies what is marked, or the focused line when nothing is (`rubraview_grid_selection_*`: a wide character whole, trailing spaces dropped, CRLF between rows).
- Consequences: what is copied is the text as drawn, sliders and brackets included. The page list on the left is not marked. Measured on the Windows 11 VM the same day: a drag across the General page copied exactly its rows (read back with `Get-Clipboard`); Sort by size put `a_photo.jpg` at 5 of 6 as the sizes say, descending at 2 of 6, natural again at 1 of 6.

## 2026-09-28: D-44 The toolbox's seek bar is a page bar for still pictures

- Status: Accepted (owner 2026-09-28: "정지화상 볼때 툴바에 동영상재생막대랑 같은 모양으로 이번 폴더나 압축파일 내에서 몇번째 이미지 보는지 진행막대", then "진행막대를 누르면 그 위치의 이미지를 현재 목록 또는 폴더에서 볼 수 있어야 해")
- Decision: with a still picture on screen and more than one page in the folder, archive or playlist, the toolbox strip's seek bar shows the page's place — (page) / (count) filled, so the last page fills it — and the line under it ends with `page / count`. A click on the bar goes to the page whose share of the bar it landed in (`rubraview_pagebar_fraction`, `rubraview_pagebar_page`), in the same order the pages are turned. The detached toolbox window does the same.
- Consequences: a film keeps its seek bar. Dragging along the page bar is not in this (a click jumps). Measured on the Windows 11 VM the same day: six pictures, 1 / 6 filled a sixth, a click at the right end gave 6 / 6 and at the middle 4 / 6, as the window title said.

## 2026-09-28: D-43 The reader's own favourites on the places bar

- Status: Accepted (owner 2026-09-28: "추천대로 진행", on a star in the path bar and a file of its own)
- Decision: a star after the path (☆, ★ when starred) and `Ctrl+D` put the folder on screen into the favourites or take it out. Favourites come first on the places bar, in the order they were added, set a little apart from the fixed places, and are kept in `favorites.ini` beside `history.ini` (portable or AppData, §3.17.2): one `[favorite-N]` section with a quoted `path` each, D-13's INI and TOML subset (`check-conf-format` samples it). At most 32; the file is written at each change.
- Consequences: the label is the folder's own name, so two favourites with the same name look alike (the lit one is where you are). Renaming or reordering favourites is not in this. Measured on the Windows 11 VM the same day: starred by `Ctrl+D`, kept across a restart, opened by a tap, removed by a tap on ★.

## 2026-09-28: D-42 The picker's places bar and typed path

- Status: Accepted (owner 2026-09-28: "메뉴에서 파일을 열고자 했을 때, 루브라뷰 있는 곳 상위 폴더는 열 수가 없었습니다 ... 운영체제별로 자주 쓰는 위치 ... 즐겨찾기 같은 곳에 넣을 수는 없나요", then "경로를 직접 입력하는 기능도 당연히 필요해요")
- Decision:
  - Every folder the picker lists is made absolute first (`rubraview_pal_fs_absolute`): it held `.` when the viewer had been started with nothing, and `.` has no parent by name, so the folders above the viewer's own could not be reached.
  - Under the path, a places bar always shows the PC page's entries (Home, Desktop, Documents, Downloads, Pictures, Videos, Music and the drives); the one on screen is lit. Only the ones that fit the window's width are shown; the rest are on the PC page.
  - `Ctrl+L`, or a tap on the path bar past its chips, turns the bar into a text box holding the folder on screen, all of it selected. Enter shows a folder or opens a file; quotes and spaces around the path go, a bare `C:` is its root, and a name without a root is taken from the folder on screen (`rubraview_picker_typed_path`). A path that is not there is said, and the box stays open to be put right.
  - An open text box takes every key before anything else: a resume offer still on screen took the box's Enter (found on the VM).
- Consequences: the grid starts 40 px lower. Favourites the reader adds themselves are not in this (they need a file of their own; asked of the owner). `~` and `%USERPROFILE%` are not expanded. Measured on the Windows 11 VM the same day: the bar, a lit Home, `..` typed from an archive's folder, a typed folder name, a wrong name reported with the box kept open, Backspace in the box.

## 2026-09-28: D-41 The picker reaches the whole machine through a PC page

- Status: Accepted (owner 2026-09-28: "탐색할 때 현재 실행 폴더만 보게 하지 말고 시스템 전체 다 볼 수 있게 탐색하도록 해 주세요"; read by the implementer as the in-app picker, which with nothing open started in `.`, the folder the viewer was run from, and stopped at a drive's root)
- Decision:
  - A "PC" page (`RUBRAVIEW_PICKER_PLACES`, `rubraview_pal_fs_list_places`) lists the usual folders that exist (Home, Desktop, Documents, Downloads, Pictures, Videos, Music — asked of Windows, since they can be moved), then every drive, as `C: <label>`, `E: Removable`, `F: CD/DVD`, `Z: Network`. On the host build it is the home folders and `/`.
  - `..` at a root (`C:`, `C:/`, `/`, `//server/share`) opens the PC page (`rubraview_picker_parent`); the breadcrumb always starts with a `PC` chip; with nothing open the picker starts on the PC page.
  - Nothing on the PC page reads a drive: only a fixed disk is asked for its label, and its tiles get no thumbnail, so an empty reader or a lost network drive cannot hold the picker or the thumbnail thread. The viewer sets `SEM_FAILCRITICALERRORS`, so opening an empty drive shows an empty folder rather than Windows' "no disk" box.
- Consequences: SPEC §15's "quick drive chips" are this page rather than chips in the header. A network share that is not mapped to a letter is not listed (typing a path is backlog). Measured on the Windows 11 VM the same day with a mouse: started with no file, `O` opened the PC page (seven folders, `C:`, `D:`, `E: CD/DVD`); `E:` opened with `..` first; `..` came back; `C:` listed the root; the `PC` chip came back from a folder three levels below `C:`. Not measured: an empty optical or card-reader drive and a lost network drive (the VM has neither).

## 2026-09-25: D-40 Zooming into a reduced page decodes the part on screen again, in tiles

- Status: Accepted (owner 2026-09-25: "타일 디코딩으로 확대 선명하게 해 주세요", after D-39 named tiled decoding as the next step)
- Decision:
  - When a page shown reduced (D-39) is magnified on screen — its texture drawn more than 5 % larger than its own pixels — the part in view (plus half a tile around, for a pan) is decoded again from the file in 512-pixel tiles at the coarsest level still as sharp as the screen (level L = one output pixel per 2^L picture pixels), and drawn over the reduced texture (`src/core/tiles.c`, `rubraview_tiles_*`).
  - The tiles are made on a thread of its own (`pal_tiles_win32.c`, the thumbnail thread's pattern, D-35): its own COM and WIC factory; the page's file read once; one band per tile row — the rows above are decoded to reach it either way — clipped, scaled and converted the way the page itself is (EXIF turn, colour profile, then clipper → Fant scaler → premultiplied BGRA; `rubraview_wic_region_pbgra`). Only the newest request counts; a failed tile is reported so it is not asked for again.
  - The main thread keeps up to 96 tile textures (about 96 MB), the least recently drawn going first, and asks only when the list of missing tiles changes. Tiles go when the page changes, when pages are unloaded (a lost device included) and at exit; none are drawn during a page fade.
- Consequences: an archive page is not tiled yet (it has no file for the thread to read). A turned (EXIF 5-8) picture's tiles go through an uncached rotator and are slower. Only level 0 could be seen on the VM: with the 128 MP budget a reduced texture is magnified only above 0.6x, where level 0 is chosen; coarser levels begin with pictures several times larger and rest on the host tests.

## 2026-09-25: D-39 Two pages share one height; a picture too large for the card is shown reduced

- Status: Accepted (owner 2026-09-25: "초고해상도 처리를 위한 폴백 등도 있어야 한다고 봐요 그리고 2개씩 보기에서 왼쪽에 저해상도 오른쪽에 고해상도 이렇게 있을때에도 저해상도가 작게 보이는게 아니라 지능적으로 크기 리사이즈해서 보여줘요")
- Decision:
  - Two-page view brings both pages to the taller one's height before the pair is fitted (`rubraview_compose_spread`), so a low-resolution scan beside a high-resolution one is the same size on screen. Actual size is the exception: there each page keeps its own pixels, centred, as before. Each draw command carries its own scale (the pixel grid uses it).
  - A page's texture is at most the device's largest bitmap side (`GetMaximumBitmapSize`) and 128 megapixels (512 MB); a bigger picture is decoded through WIC's Fant scaler to what fits (`rubraview_fit_within_limits`). When the device refuses even that, the size is halved and tried again, down to 1024 pixels. The page keeps the picture's own size for layout, zoom, crop and the status line ("shown reduced"), and its texture is stretched to it.
  - The adjust panel's preview of such a page reads the picture through the scaler at the window's size (`rubraview_pal_image_read_pixels_within`), and so does any page whose full-size copy cannot be allocated. Save a copy still works at full size or says it could not (T093).
- Consequences: a picture over 128 MP that the card could have held whole is now held reduced, so zooming in past the reduced size is softer than it was there; pictures the card refused now open at all. Region (tiled) decoding for deep zoom would be the next step. The budget is the implementer's pick. The VM's software renderer accepted a 20000x12000 bitmap, so the halving step after a refusal was not seen to run.

## 2026-09-25: D-38 Bicubic and Lanczos resizes go to the graphics card, with the CPU's own arithmetic

- Status: Accepted (owner 2026-09-25: "gpu 가속으로 확대 축소 더 빨리 하는거 고민하고 처리해 주세요. 테스트는 못하겠지만요")
- Decision: on-screen zoom already runs on the card (Direct2D); the CPU work was export's and batch's resizes. Those now go to a D3D11 compute path when `[display] gpu_resize` allows it: two shaders (horizontal into a float intermediate, then vertical) fed the CPU's own weight tables, `precise` and IEEE-strict so they add in the CPU's order; the output bands keep the intermediate under 32 MB. The shaders are compiled at run time by Windows' `d3dcompiler_47.dll` (the build host has no HLSL compiler). `on` (default): real cards only, for resizes of a megapixel or more; `always`: also Windows' software adapters (for testing); `off`: never. Any failure — no compiler, no card, a source over 256 MB, a device lost mid-way — falls back to the CPU. A batch run also resizes on every core (RV-067's pool, byte-identical).
- Consequences: measured on the VM through the Basic Render Driver: identical bytes to the CPU on six resizes and on a whole batch; the speed on a real card is not measured (no card here) — the expected gain is on enlargements, and upload and readback limit it on small pictures, hence the megapixel threshold. Gray and 16-bit pictures stay on the CPU.

## 2026-09-25: D-37 Bicubic and Lanczos resample in two passes; output may move by one level

- Status: Accepted (owner 2026-09-25: "남은 백로그도 다 처리 바랍니다", which included the separable resampler left open by D-32 because it changes output)
- Decision: bicubic and Lanczos-3 filter source rows horizontally once (per 128-column block, a stack cache of float rows) and sum them vertically; when the image shrinks vertically by taps - 1 or more (3 for bicubic, 5 for Lanczos), the previous 2-D sum is used instead, being cheaper there. The intermediate stays float.
- Consequences: output may differ from 0.0.17's by one level in a few bytes (measured: at most 1, in at most 0.003 % of bytes); bilinear and nearest are unchanged. Enlarging is 3-13x faster, moderate shrinking 1.2-1.9x, large shrinking unchanged (`docs/benchmark/results/2026-09-25-resample-two-pass.md`). The previous implementation is kept as `tools/resample_reference.c` for `tools/resample_compare.c`.

## 2026-09-25: D-36 Dropped virtual files are copied to a temporary folder and opened from there

- Status: Accepted (implementer, carrying out RFC-0001 §3.19.2 / RV-073, which already called for an OLE `IDropTarget` taking drops from browsers and archive managers)
- Decision: the main window registers an OLE drop target (falling back to `DragAcceptFiles` when OLE cannot be had). CF_HDROP gives paths as before. A drop that offers only virtual files (FileGroupDescriptorW + FileContents) has each file written to `%TEMP%\rubraview-drop-<pid>\<n>\<name>`, the name cleaned by `rubraview_path_safe_name`, at most 16 files and 1 GB each, and the drop opens those copies. The folder is this process's and is removed when the window is destroyed. The effect offered back is copy (or link), never move.
- Consequences: a dropped picture is a copy — renaming or deleting it from the viewer acts on the temporary copy, not on anything in the source program. Links and plain text are refused (no download).

## 2026-09-25: D-35 Thumbnails are made on a thread of their own; archives too, within a time limit

- Status: Accepted (owner 2026-09-25: "썸네일은 다 읽고 파일 목록 보여주는게 아니라 동적으로, 일단 파일 목록 보여주고 입력 처리하면서 백그라운드로 동적으로 될 떄마다 추가하는 형식으로 항목에 표시하게 해줘요" and "압축파일도 썸네일 보여주게 해 주세요. 다만 제한시간 걸고 읽는 속도나 시간이 오래 걸릴 것 같으면 포기하는 식으로.")
- Decision: D-34's thumbnails move off the main thread. The list shows at once; the main thread only asks for the tiles on screen and turns finished pictures into textures, a result waking the loop. One worker thread (`src/pal/win32/pal_thumbs_win32.c`, below normal priority) owns its COM apartment, WIC factory and scratch arena and does the shell calls, folder listings and archive reading. Requests are taken newest first (`src/core/thumbq.c`), so after a scroll the tiles now on screen come first; a new listing is a new generation and late results are dropped. Archives: a ZIP (CBZ) is read in pieces — the directory at its end, then the first page's entry — into a buffer of the file's size, a megabyte at a time, and given up as soon as the rest could not be read within 1.5 s at the speed seen so far (`rubraview_read_budget_ok`); an entry over 24 MB, a file over 2 GB, or a 7z/CB7 over 24 MB (solid: its first page may need all of it) is not tried. The page is decoded small by WIC from memory.
- Consequences: supersedes D-34's "archives keep the plain tile" and "two per pass on the main thread". No memory-mapping: a mapped file on a network drive that goes away would fault inside the viewer; explicit reads fail instead.

## 2026-09-25: D-34 The picker's tiles show the item's picture, soft, with an outlined name

- Status: Accepted (owner 2026-09-25: "파일/폴더 선택할 때 썸네일이 박스 안에 그려지면 좋겠네요. 글자는 서로 다른 외곽선과 내부 색으로 표시하여 그림 위에서도 잘 보이게 하고, 썸네일은 선명하게가 아니라 약간 흐릿하게 처리하고, 폴더도 썸네일 보이게요.")
- Decision: each picker tile shows the picture Windows' shell has for the item (photos and films alike), cut to the tile's shape, made small (96 px wide) and stretched back, box-blurred by one pixel and darkened to 78 %; the name is drawn in a light cream with a black outline. A folder shows its first picture in natural order, or its first film when it holds none — its own files only, not its subfolders. Thumbnails are made two per pass of the loop, for the tiles on screen only, in a scratch arena emptied after each, and given back when the picker closes.
- Consequences: archives keep the plain tile (their first page would mean reading them whole). A file the shell has no thumbnail for keeps the plain tile. The shell's parser refuses '/' as a separator, which the listings use — `shell_thumbnail_bgra` turns it into a backslash (this also mends the shell cover of a sound file opened from a listing). Host-tested in `src/core/thumb.c` (T089).

## 2026-09-25: D-33 Text subtitles in a box the reader moves and sizes; the Sub tile toggles and chooses

- Status: Accepted (owner 2026-09-25: "자막 출력 부분을 떠다니는 반투명 창처럼 하면 좋을 것 같네요. 터치하면 창 외곽선이 굵게 보이고, 그 상태로 오른쪽 상단 위에 S(설정) M(이동) R(리사이즈) X(종료) 버튼이 뜨고, 이동이나 리사이즈는 버튼 위에서 드래그하게요. 툴박스에 sub 버튼이 있어서 그거 누르면 자막이 꺼지고 켜지고, 더블클릭이나 프레스 홀드 하면 여러 자막 중 원하는 언어 자막 열 수 있게 하고요.")
- Decision: text subtitles are drawn in a translucent box inside the viewer's window (not an OS window: it must sit over the film in fullscreen). A tap selects it — thick edge, S M R X above its top-right corner, answering before the rest of the chrome; a tap elsewhere deselects and does nothing else. M dragged moves it inside the window; R dragged holds the bottom-left corner and sets the width and the height, and the height *is* the subtitle size (`video.subtitle_size` is written, so the settings page and the box never disagree). S opens Settings › Video; X turns subtitles off. The box's shade is `video.subtitle_background` (0-100 %, default 35). An empty box draws nothing and takes no taps. Its place is kept in `layout.ini` `[subtitle_box]` as fractions of the window. The toolbox's Sub tile is `toggle_subtitles`: a tap turns subtitles off and back on to the track last shown; a second tap within 0.35 s or a press held 0.5 s opens a list (Off, then each track) above the tile — so this one tile acts on release. The menu's Subtitles gains On/off and Choose; `C` still cycles.
- Consequences (implementer's choices the owner can overrule): DVD and Blu-ray picture subtitles stay where the disc put them (D-22, D-27) and obey on/off; the list is a small popup of its own rather than a submenu. Logic is host-tested in `src/core/subbox.c` (T088). The window declines Windows' touch press-and-hold and flicks (`WM_TABLET_QUERYSYSTEMGESTURESTATUS`), so a finger's hold arrives as a held left press.

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
