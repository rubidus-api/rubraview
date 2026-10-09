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

## 2026-10-09: D-80 File types: by kind, for this user or for every user, and given back on removal

- Status: Accepted (owner 2026-10-09: "확장자 연결 기능 설정 창에 넣어주세요. 현재 사용자만/관리자 전체 등 다양하게요")
- Decision:
  - Settings › General › File types opened with Rubraview: four kinds (`shell.pictures`, `shell.comics`, `shell.video`, `shell.music`, all on), and five rows — register for this user (HKCU), register for every user (HKLM), the two removals, and Windows' Default apps (`ms-settings:defaultapps`).
  - For every user the viewer runs itself again as an administrator (`runas`; Windows asks) with `--register-shell` / `--unregister-shell`, `--all-users` and `--types=`; it never writes HKLM from the reader's own process. Without an administrator's rights `--all-users` fails and says so.
  - Registering also adds the ProgID to the extension's `OpenWithProgids`, and keeps the default it replaces in a value beside it (`Rubraview.Was`). Removing puts that default back, removes only our values, and deletes the extension's key only when nothing is left in it. Before, removal deleted the whole key when it pointed at us — under HKLM that would have taken another program's entries with it (seen as a risk on the VM, where an elevated shell ran the all-users path).
  - Windows keeps a default chosen by hand (UserChoice) whatever is registered; the page says so and opens Default apps.
- Consequences: the kinds and their parsing are in `filemanage.c` and host-tested; registering, removing and restoring were measured on the VM for this user and, from an elevated shell, for every user (T128). Not measured: the administrator question itself (the `runas` path from the settings window).

## 2026-10-09: D-79 Five layouts, one setting: single, dual, book, webtoon, comic

- Status: Accepted (owner 2026-10-09: "2장보기 아이콘에서 누를 때마다 ... 순환", "아이콘만 1장-2장-북-웹툰 ... 웹툰 스타일은 그냥 세로로 길게 자연스럽게 이어지는 것 ... 폭을 똑같이", "설정의 한장 두장 책 웹툰 설정과 화면 보는 방식이 어긋나 있어요 ... 코믹 추가해서 가로로 긴 파일은 좌우로 반반 나눠서 보여주는 기존 방식 ... 보기 방식은 5종")
- Decision:
  - `viewer.layout` (single | dual | book | webtoon | comic) is the only place the layout lives. `toggle_layout` (`B`, the Layout tile), the menu's five items (`layout_single` ... `layout_comic`) and the settings window write it; applying the setting sets the mode. It was a "default layout" before, read once, and the key changed a copy — the mismatch the owner saw.
  - The Layout tile shows the layout in use (Segoe MDL2 Page, TwoPage, ReadingMode, ScrollUpDown, PreviewLink) and is in the picture toolbox as well as the archive one.
  - Webtoon is a strip: every page at one width (the width of the page it was entered on, no wider than the window, times the zoom), one under the other with no gap. The place is the page at the window's top and an offset into it (`rubraview_strip_scroll`); that page is the current one for the title, the page bar and the lists. The wheel moves it 120 px a notch, a drag or a touch pan moves it, `next_page` / `prev_page` move a window less a tenth and go on to the next archive at the end, `Home` / `End` go to the first page's top and the last page. A film, a song or an animation on the top page is shown the ordinary way.
  - Comic is single with a wide scan (width / height >= 1.15) shown as its two halves in reading order — what "Split wide spreads" did in every layout. That setting is removed: single, dual and book show a wide scan whole. A page the reader turned on its side is still never split (D-77).
- Consequences: host tests for the strip's scrolling and ends and for comic (T127); the strip, the cycle, the icons and the saved setting were measured on the VM. Not done in the strip: the crop overlay, the page-turn fade, the slide show's timing by page, sharper tiles for more than the page across the window's middle. A `spread_autosplit` line left in an old settings.ini is ignored.

## 2026-10-09: D-78 The picker leaves the top to the title bar

- Status: Accepted (owner 2026-10-09: "파일 및 폴더 열기 화면에서, 위에 창 타이틀 플로팅이 뜨지 않아 답답합니다. 여기에는 여백 또는 클릭 불가능한 정보 등이 들어가도록 하고 플로팅 타이틀바가 뜨게")
- Decision: with the frameless window, the picker starts with a band the title bar's height that says what the screen is ("Open a file or a folder" / "Pages of this book") and takes no click; the path, the places and the tiles sit under it. The floating title bar is drawn over the picker too and its buttons answer first; its caption is the folder's path. With Windows' own title bar there is no band. The icon's eye is white on the red square (owner, same day: the black lines were hard to see).
- Consequences: measured on the VM (the band, the bar over it, Minimise from it). The title bar's drawing and its clicks are functions of their own (`draw_titlebar`, `titlebar_click`).

## 2026-10-08: D-77 The reader's rotation with two pages; the information bar only when asked for

- Status: Accepted (owner 2026-10-08: "2장보기 모드에서 rotate 시켰을 때, 90도일 때와 180도일 때 화면이 이상하게 보입니다 … 돌린 이미지 기준으로 비율을 봐서 가로로 길면 1장씩, 세로로 길면 2장", and of the translucent bar with the picture's size: "이거 없앴으면 좋겠는데요")
- Decision:
  - Pairing goes by the page as turned (it already did): wide stands alone, tall pairs. A page that a quarter turn laid on its side is one page shown whole; "Split wide spreads" does not cut it in two. That was the fault at 90 degrees: the turned page was split, and a split half was drawn from the unturned picture.
  - A half turn turns each page where it stands; the pair keeps its sides. (0.0.35 made the two pages change places at a half turn, the implementer's reading of "이상하게" at 180 degrees. Withdrawn in 0.0.36 — owner 2026-10-09: "180도일 때 순서 바꾸는 동작 취소바람. 내가 270도를 잘못 쓴거야": the fault reported was at 90 and 270 degrees.)
  - A split half is drawn through the orientation, so a wide scan turned over or mirrored shows the right half the right way up.
  - Turning or mirroring keeps the page: of a pair, the earlier one. Before, the spread's number was kept while the spreads were made again, so the book jumped.
  - The information bar (the picture's size and the zoom, along the bottom) no longer comes up by itself at every page. `Shift+I` and Menu › View › Info still turn it on, and then it stays.
- Consequences: `rubraview_layout_opts_turned` and `rubraview_orientation_region` are host-tested (T125); the rest was measured on the VM. Not done: the crop overlay on a split half of a turned page; remembering the bar's state across runs (it starts off).

## 2026-10-08: D-76 The file list window (F3): the archive's files, a preview, in a window of its own

- Status: Accepted (owner 2026-10-08: "압축파일 내부 파일들 목록들을 볼 수 있고 거기서 이동도 가능하며 프리뷰도 보여주는 압축파일 내 파일목록창", then "위의 창은 모달리스여야 … 원래 창은 원래대로 계속 보여주고 있고 외부의 창으로 파일을 제어")
- Decision:
  - A separate window owned by the viewer ("Rubraview files"), like the information window and the mini player: its own renderer and message queue, resizable, its place remembered for the session, closed by `F3`, `Esc` or its close button. It is not the `P` list, which floats inside the viewer and stays as it was.
  - Left: the files of what is open — an archive, and also a folder or a playlist, since the list is the same thing — numbered, with their sizes (an archive's index gives them; a folder's files are not asked), the one on screen marked and kept in view. Right: a preview fitted to its room, with the name, the picture's pixels and its size.
  - Pointing at a row previews it after the pointer has rested 0.12 s; `Up` / `Down`, `PageUp` / `PageDown`, `Home` / `End` move a selection of the window's own and preview at once; a click or `Enter` turns the viewer to the file. Every other key pressed there goes to the viewer's own key handling.
  - The preview is decoded by that window's renderer from the same bytes the viewer reads (a texture belongs to one renderer). Not decoded for a glance: a film or a sound, and a page that costs more than 32 MB of decoding to reach inside a solid archive (the viewer's own threshold for a far read); neither while the viewer's far read is running.
  - Key `F3` (free; beside `F2` rename and `F4` filmstrip). Toolbox "Files" after "List" in the archive, image, multi-page and animation profiles; Menu › File.
- Consequences: geometry, the selection step and the preview fit are in `ui_listwin.c` (`rubraview_filewin_*`) and host-tested; the window itself is measured on the VM (T124). Not done: only the files the viewer shows are listed (pictures; an archive's other files are not), a search box, thumbnails in the rows, remembering the window's place across runs.

## 2026-10-08: D-75 Copy to a folder (F6) and move to a folder (F7)

- Status: Accepted (owner 2026-10-08: "현재 보고있는 파일을 미리 지정한 특정 폴더에 저장하거나, 혹은 옮기는 기능 … 두개 따로요. 압축파일 안의 이미지/영상인 경우에는 복사만 허용 … 겹치는 이름이 있을 경우에는 … 뒤에 -1 … -2")
- Decision:
  - Two actions, `copy_to_folder` and `move_to_folder`, with a folder each: `[curation] copy_dir` and `move_dir` (Settings › Files › The file on screen, to a folder). They are beside the number-key folders (§3.18.3), which keep working as they did.
  - Keys: `F6` copies, `F7` moves (both were free; `F5` beside them is the slide show, `F2` rename). Toolbox: "Copy to" and "Move to" after Export / Rename and before List in every profile, with Segoe MDL2's Copy and MoveToFolder icons; the archive profile has Copy to only, and the Move tile is dimmed wherever an archive page is on screen. Menu › File has both.
  - A page inside an archive is copied by writing its bytes as a file named after the page; moving it is refused with a notice. The archive is never changed (editing archives is R149).
  - A name already in the folder is never replaced: `name.ext`, then `name-1.ext`, `name-2.ext`, ... — the first free number, before the extension (`rubraview_filing_target`).
  - A move into the folder the file is already in does nothing and says so; with no folder set the notice says where to set it. Both are undone by `Ctrl+Z` like the number keys' moves and copies.
- Consequences: `test_filemanage` covers the naming and the two settings; VM measured (T123). Not done: choosing the folder at the moment of the key press when none is set; several preset folders per action (the number keys do that).

## 2026-10-08: D-74 Every archive is read and written by FultaArc

- Status: Accepted (owner 2026-10-08: "fultaarc 벤더링 해서 코드 고치고"; asked what becomes of ALZ and RAR 1.5: "FultaArc 범위만"; file associations: ".cbr·.cbz·.cb7만"). Carries out RV-100; supersedes D-68 (the writer here) and D-73 (the RAR decoder here) as to where the code lives.
- Decision:
  - `vendor/fultaarc` (FultaArc 0.1.0, commit `e3dcb85`, MIT; the library that was started from this project's archive work) is the only archive code: ZIP, 7z and RAR 2.0-7.0, solid, split and password-protected, and ZIP / 7z writing, through `fulta/arc.h`. `src/core/pagesource.c` is rewritten on it; the viewer's own readers and decoders (`archive.c`, `sevenzip.c`, `rar.c`, `rarcodec/`, `alz.c`, `archive_write.c`), their headers and tests, and `vendor/lzma`, `vendor/miniz`, `vendor/bzip2` leave the tree. FultaArc brings its own LZMA SDK, miniz, bzip2 (unmodified) and zstd, and is built on this project's `vendor/proven`, moved to the same snapshot (`981911b`).
  - What FultaArc does not ship yet is not read: ALZ, EGG and RAR 1.5 (FultaArc holds them until its clean rooms deliver). ALZ and RAR 1.5, read by 0.0.30-0.0.31, stop being read until then.
  - Names: a code page the reader chooses (`Shift+N`) is applied by FultaArc; with none chosen, names that are not Unicode are still read in the machine's code page, from the stored bytes, as before.
  - The format documents (`docs/specs/`) leave the public tree with the code they described; FultaArc publishes its own.
  - Explorer associations stay the picture, film, music and comic-book ones: `.zip` and `.alz` leave the list; `.cbz`, `.cb7`, `.cbr` stay.
- Consequences: gained with FultaArc — encrypted ZIP and 7z, more ZIP and 7z methods (bzip2, LZMA, PPMd, zstd, Deflate64, the old PKZIP ones), every page's checksum verified (a damaged page is refused rather than shown in part). Lost — ALZ, RAR 1.5, and a 7z's progress while it skips inside a solid block (the read still runs off the main thread and can be cancelled). `test_archives` reads the RAR fixtures' three manifests through the page source; the per-decoder tests went with the decoders. Backlog RV-092 to RV-099 (ALZ, EGG and AZO work here) are closed as FultaArc's.

## 2026-10-02: D-73 RAR is read by the project's own decoder

- Status: Accepted (owner 2026-10-01: "rar도 구현 바랍니다", plan `mit-archives`; the work of the `rar-spec` and `rar-decoder` clean rooms, collected 2026-10-02).
- Decision: `src/core/rar.c` (the header reader) and `src/core/rarcodec/` (RAR 2.0, 2.9/3.x with PPMd and the six standard RAR 3 filters, 5.0 and 7.0; AES, SHA-1/256, HMAC, PBKDF2, BLAKE2sp) are MIT, written by a session that never saw UnRAR, from `docs/specs/rar-decompression.md`, which another session wrote from BSD-licensed sources (libarchive 3.7.7, rardecode) and RARLAB's technote. The codec is registered at start; `rar_codec.h` is version 2. The header reader written beside UnRAR (D-63, D-65) and its tests are replaced.
- Consequences: CBR/RAR files that 0.0.26-0.0.29 read with UnRAR's code are read again, with no UnRAR code anywhere (D-70). RAR 1.5 compression waits for `rar15-blackbox` (D-72); a non-standard RAR 3 filter program is "not supported". The 4.5 GB fixtures are tested with `RUBRAVIEW_RAR_BIG_TESTS=1`.

## 2026-10-02: D-72 RAR 1.55 decoded from a black-box clean room; no general RAR 3 VM

- Status: Accepted (owner 2026-10-02, confirmed directly: "구현해보려 합니다. 맞습니다."). Supersedes D-71.
- Decision:
  - RAR 1.5 compression (unpack version 15) is worked out by a clean-room session of its own (`rar15-blackbox`) from the behaviour of RAR 1.55 for DOS, run in DOSBox on inputs the session chooses: output only, no disassembly, and no outside description of the format. Every test and inference is logged step by step in the copy, and the resulting spec (`docs/specs/rar15.md`) cites the log. The decoder is written from that spec, MIT.
  - The general RAR 3 virtual machine is not implemented: it would run programs carried by an archive, and RARLAB's programs write only the six standard filters. A file with any other filter program is "not supported".
- Done (2026-10-02): the spec (`docs/specs/rar15.md`) and `src/core/rarcodec/unpack15.c`, from the black-box clean room; wired into the codec in the rar-decoder session's second round (streamed, one state across a solid archive's files; a RAR 1.55 volume without an end block continues into the next). `expected-rar15.txt`: 74 files of 22 archives.
- Consequences: RAR 1.55 itself is used as a black box only and is never committed or redistributed. Limitation the owner accepted (2026-10-02, in the clean-room session, option (a)): the session is a language model whose training data very likely includes UnRAR and other descriptions of RAR 1.5, so the log cannot prove that the choice of hypotheses was free of that; every rule is still confirmed against RAR 1.55's own output, and every log line marks its hypothesis `src=obs` (from earlier observations) or `src=prior` (from general knowledge, possibly recall). Encryption of RAR 1.5 archives, the RAR 1.4 format and any compressor are out of scope.

## 2026-10-02: D-71 RAR 1.5 compression is not supported

- Status: Superseded by D-72 (same day); was Accepted (owner 2026-10-02: "지원 안 함으로 확정", asked whether RAR 1.5 decoding should be specified from The Unarchiver's LGPL code, worked out from DOS RAR 1.5x output alone, or left out).
- Decision: files compressed with RAR 1.5's algorithm (unpack version 15, archives made by RAR 1.5x in 1994-1996) are not decoded; the RAR reader lists them and says the method is not supported. No permissively licensed, independently written description or decoder of that algorithm was found: the known ones are UnRAR, code derived from it, 7-Zip's (under the unRAR restriction) and The Unarchiver's (LGPL).
- Consequences: the clean-room RAR decoder covers RAR 2.0, 2.9/3.x, 5.0 and 7.0 compression. The old RAR programs downloaded for test data are kept with the owner's Windows packages, outside the repository.

## 2026-10-01: D-70 No UnRAR code in the program

- Status: Accepted (owner 2026-10-01: "vendor/unrar_proprietary/ 이 자료는 ai-share/archive 밑으로 옮겨주세요. 우리는 unrar 안쓸거니까요"); supersedes D-66's module and D-63's use of UnRAR.
- Decision:
  - The C conversion of UnRAR leaves the repository (kept outside it, in the owner's archive). `unrar_proprietary.dll` is no longer built, shipped or loaded: the program's start-up lookup for it is removed, so a copy of the DLL beside the program is ignored.
  - `include/rubraview/rar_codec.h` stays as the table a RAR decoder fills; the decoder will be the project's own, written in a clean room from `docs/specs/rar-decompression.md` (plan `mit-archives`), and registered with `rubraview_rar_set_codec`.
  - Until then the RAR header reader opens a CBR / RAR whose pages are stored uncompressed and unencrypted; a compressed or password-protected one says that RAR decoding is not in this version.
- Consequences: the UnRAR notice and licence leave THIRD_PARTY_NOTICES, the F1 help and the release zip; the README and the manual say what opens. Releases from here until the decoder lands read fewer CBR files than 0.0.26–0.0.29 did. `src/core/rar.c` (the header reader) was written beside UnRAR and is to be replaced by the clean-room one.

## 2026-10-01: D-69 The public repository holds the sources, the manual and the specs

- Status: Accepted (owner 2026-10-01: "꼭 공개해야 하는 소스 코드와 매뉴얼, 스펙들 빼면 다 비공개 쪽으로 돌리고"; then: tests, tools and scripts private; the design RFCs and this file public; the history rewritten)
- Decision:
  - `.gitignore` is an allowlist: `README.md`, `README.ko.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `DECISIONS.md`, `Makefile`, `src/`, `include/`, `vendor/`, `resources/distribution/`, `docs/manual/`, `docs/specs/`, `docs/rfc/`, and the two packaging scripts (`scripts/package.sh`, `scripts/gen-notices.py`). A new file stays local until it is allowed on purpose.
  - Everything else (the working docs, plans, the change log, tests and their fixtures, tools, operating scripts) is kept in a separate private repository and is not published.
  - The history was rewritten to the same surface, so earlier commits do not carry the private files either; the release tags were moved to the rewritten commits.
- Consequences: the public repository builds the program (`make win64`) but does not carry the tests (`make test`). The licence notices point to each `vendor/<name>/VENDORED.md` for provenance. Before the split, the bootstrap kit's blocklist `.gitignore` had published `docs/`, `tests/`, `tools/`, `scripts/` and `CHANGELOG.md` since the first commit (2026-09-07).

## 2026-10-01: D-68 Writing ZIP and 7z, all of it ours or public domain

- Status: Accepted (owner 2026-10-01: "7z와 zip는 mit license로 만들 수 있다는 말이지요? ... 압축 지원은 7z와 zip까지"; plan `mit-archives`, step 5)
- Decision:
  - `src/core/archive_write.c` (MIT, written for this project) writes a list of entries held in memory as one archive, through a sink that appends and may rewrite bytes already written.
  - ZIP / CBZ from PKWARE's APPNOTE: local headers and central directory, deflate (miniz's `tdefl`, MIT) where it is smaller and stored otherwise, the UTF-8 name flag for names that are not ASCII, ZIP64 records when a size, an offset or the entry count needs them, MS-DOS time and Info-ZIP's extended timestamp (UTC).
  - 7z / CB7 from the LZMA SDK's `DOC/7zFormat.txt`: the start header, one solid folder of the non-empty files, LZMA (the SDK's `LzmaEnc`, public domain, added to `vendor/lzma`) or Copy at level 0, sizes and CRCs per file, empty files, UTF-16 names, modification times; the dictionary no larger than the data.
  - Names are refused when empty, absolute, with a `.` or `..` part, an empty part or a control character; `\` becomes `/`.
  - Nothing in the viewer uses it yet: it is for RV-090 / R149 (an edited book, a RAR converted to 7z or ZIP).
- Consequences: `test_archive_write` writes ZIP and 7z at levels 0-9, ZIP64 forced, archives of empty files only and empty archives, and reads every one back through the program's own readers byte for byte; it refuses bad names, stops on every write a sink refuses, and cancels. Outside check (manual, T118): the system's `7z t` ("Everything is Ok"), `unzip -t` ("No errors detected") and Python's `zipfile.testzip()` accept the ZIP, the ZIP64 ZIP and the 7z; `7z l` shows the LZMA method, the Korean name, the times and the CRCs.

## 2026-10-01: D-67 An archive's names read again in a code page chosen for it alone

- Status: Accepted (owner 2026-10-01: "shift-jis 나 중국, 대만에서 널리 사용되는 인코딩으로 압축파일 내 파일명을 해석할 방법 ... 전역 설정으로 매번 바꾸는게 아니라 특정 파일 열 때마다 일회성으로 ... 현재 열고있는 압축파일을 새로 파일명을 해석하는 기능")
- Decision:
  - File › Names in archive (Auto, UTF-8, Korean, Japanese (Shift-JIS), Chinese (GBK), Chinese (Big5), Western) and `Shift+N` (the next of them, round again) open the archive on screen again with its names read in that code page, the same page kept on screen, and say "names read as <code page>: <the page's name>". Settings › Files › Archive filenames is not touched: the choice belongs to that archive while it is open, and any other archive opens with the setting again.
  - It changes the names an archive stores without saying they are UTF-8: ZIP / CBZ without the UTF-8 flag and RAR 4 without Unicode names; 7z and RAR 5 names are Unicode already.
- Consequences: measured on the Windows 11 VM (Korean system code page) with three CBZs whose names are raw Shift-JIS, GBK and Big5 bytes: Auto showed `묉덇쁞/긻?긙01.jpg`; `Shift+N` to Japanese showed `第一話/ページ01.jpg`, to GBK `第一话/页面03.jpg` and to Big5 `第一話/頁面03.jpg` with page 3/3 kept; the previous archive opened with Auto again; settings.ini still said `archive_codepage = "auto"`. Host builds have no code-page tables, so this is measured on Windows only.

## 2026-10-01: D-66 The UnRAR code is a module of its own: unrar_proprietary.dll

- Status: Accepted (owner 2026-10-01: "unrar 모듈은 분리하도록 해요. 라이선스가 다르니까요. ... 따로 분리하거나 대체할 수 있게요", then "파일이름은 unrar_proprietary 이렇게 하고 동적으로 dll 연결하거나 해야지요" — plan `docs/plans/archive/2026-10-01-unrar-module.md`)
- Decision:
  - `vendor/unrar-c/` is now `vendor/unrar_proprietary/`: the UnRAR-licensed `rar_unpack.c` and `rar_crypt.c`, and `unrar_proprietary.c`, which exports one function, `unrar_proprietary_api(version)`. On Windows it builds `unrar_proprietary.dll`; `rubraview.exe` contains none of it and loads it at run time (`LoadLibraryExW`, from the program's own folder only, then `GetProcAddress`).
  - The boundary is `include/rubraview/rar_codec.h` (MIT, ours): a versioned table of functions — unpack, AES-CBC, RAR 3 and RAR 5 key derivation, the CRC MAC, SHA-256. `src/core/rar.c` (MIT, ours: the headers, volumes, passwords, CRCs) calls only through it. Any DLL of that name that answers version 1 with such a table can replace this one.
  - Without the DLL: the headers still read and a stored, unencrypted CBR opens; a compressed or encrypted one says "this CBR needs unrar_proprietary.dll beside rubraview.exe" (`RUBRAVIEW_RAR_ERR_NO_CODEC`, `needs_codec` on the page source), and its picker tile has no cover.
  - The zip carries the DLL and `licences/unrar_proprietary-LICENSE.txt`; the release offers the DLL as a download of its own beside `rubraview.exe`. The host tests link the module directly and register it.
- Consequences: `rubraview.exe` is 53 KB smaller; the DLL is 173 KB and exports only `unrar_proprietary_api`; the exe has no import of it. `test_rar` checks, with no module registered, that stored files read and compressed or header-encrypted ones are refused as such, that a table of another version is not taken, and everything else with it registered. Measured on the Windows 11 VM: the exe with the DLL beside it read a compressed CBR to page 20; the exe alone read a stored CBR to page 20, showed the stored one's cover and none for the compressed one in the picker, and opening the compressed one said the DLL was missing.

## 2026-10-01: D-64 The music features finished: lyrics in the file, a cue of any name, the equaliser window

- Status: Accepted (owner 2026-10-01: "하지 않은 것들 다 처리 바랍니다" — the items D-62 left open)
- Decision:
  - The words a music file carries are shown when no `.lrc` is beside it: ID3 `SYLT` (timed, millisecond stamps; MPEG-frame stamps are left out) first, then `USLT`, a Vorbis `LYRICS` / `UNSYNCEDLYRICS` comment or MP4's `©lyr`, read as LRC when they are LRC. Untimed words scroll with the song, none marked, and a click on them does not seek.
  - A `.cue` of another name beside a song is used when its `FILE` names the song (letter case ignored; another extension of the same name matches, for a record converted after its sheet was written); opening a `.cue` itself plays the audio it names. The picker lists `.cue` files.
  - The equaliser window (`EQ bands` in the toolbox, Playback › Sound › Equaliser window): a see-through panel over the picture with the nine presets (the eight and Custom) and ten upright sliders, ±12 dB in half-decibel steps; drag, turn the wheel over a band, right-click for 0 dB. Moving a band makes the equaliser Custom, starting from the curve the sliders showed; a named preset shows its own gains, and Custom comes back as it was left. The ten gains are settings (`audio.eq_band1..10`, on Settings › Audio too); the store holds 96 settings now (it held 64).
  - Fixed on the way: the `.lrc` `[offset:]` was added and taken off again, so it did nothing (D-62's drawing and click-to-seek); a folder typed or browsed in the settings window was kept as a pointer into the box's own buffer, so the next folder typed changed it (D-60, found on the VM today).
- Consequences: measured on the Windows 11 VM: an MP3 with a Korean `SYLT` paused at 10.15 s marked "둘째 줄" (7 s); an MP3 with `USLT` showed its four lines unmarked; a FLAC whose `LYRICS` comment is LRC marked its third line at 15.4 s; opening `Disc image.cue` played `album.wav` named "Low side — Rubraview, Cue by name no. 1/2", and `PageDown` went to "High side, no. 2/2" at 10.000 s; the equaliser window opened from its tile over the toolbox, the 1 kHz band dragged to +11.0 made it Custom, the wheel took 16 kHz to −0.5, Rock showed its gains and Custom came back as left, and settings.ini held `eq_band6 = 11.0`, `eq_band10 = -0.5`. Settings › Files over an RDP session with the Korean layout: the user folder's path and `\rb` typed, then 한글 in Hangul mode and `Enter` with 글 still composing kept the folder `rb한글` (U+D55C U+AE00, read back from the clipboard); `Ctrl+O` in Folder 2's box opened "Choose a folder", and the folder chosen there was in the box and kept; settings.ini held both. The VM's settings were put back afterwards. After the measurement, the search for a `.cue` of another name was made to list a folder once rather than on every track (the working arena is never given back); built, not measured again on the VM. Not measured by ear: the equaliser's sound (host-tested, `test_audio_chain`).

## 2026-10-01: D-65 Encrypted and split RAR archives; archives edited later are converted from RAR

- Status: Accepted (owner 2026-10-01: "하지 않은 것들 다 처리 바랍니다", and for editing inside archives, "rar이나 cbr은 사용자에게 어떤 압축형식으로 변환할지 묻고(7z/cb7과 zip/cbz 중 선택) 처리하게 해요" — plan `docs/plans/archive/2026-10-01-rar-passwords-volumes.md`)
- Decision:
  - RAR 3.x/4.x (AES-128, RAR's SHA-1 key derivation of 2^18 rounds) and RAR 5 (AES-256, PBKDF2-HMAC-SHA256, the stored password check, CRCs turned into MACs) are read, the files encrypted and the headers too. `vendor/unrar-c/rar_crypt.c` converts UnRAR 7.3.1's `crypt3.cpp`, `crypt5.cpp`, `rijndael.cpp` (the generic tables, no AES-NI), `sha1.cpp` (with RAR 2.9's variant, which writes the hashed words back into the password buffer) and `sha256.cpp`. RAR 1.5 / 2.0 encryption is refused as unsupported: nothing writes it any more to test it with.
  - A locked archive asks for its password in the viewer's text box (dots, a character each; the IME works, so a Hangul password can be typed; `Ctrl+C` / `Ctrl+X` do nothing there). A wrong one says so and asks again; `Esc` leaves the archive unopened. The passwords that opened something are tried first on the next locked archive, up to eight, and live in memory only: never the settings, a log or the clipboard, and wiped when the viewer closes. A locked book has no cover in the picker.
  - A set of volumes (`x.part1.rar`, `x.part01.rar`, ...; `x.rar, x.r00, ...`) opens as one book from any of its volumes, each mapped by the page source; an entry is a list of pieces and one CBC stream across them (as UnRAR reads it), with the whole file's CRC in its last piece. A file that runs into a missing volume says so; the others still read. The picker and next/previous archive show the first volume only; the information window says "Volumes: n, read as one" and the set's size.
  - Later, when files inside an archive can be deleted or changed: a RAR / CBR is never written as RAR (UnRAR's licence forbids a RAR-compatible archiver, and RAR's compression is proprietary). The user is asked which to convert it to — 7z / CB7 or ZIP / CBZ — and the edited book is written in that format (REQUIREMENTS R149, BACKLOGS).
- Consequences: `test_rar` checks AES-128/256 CBC (NIST SP 800-38A), SHA-1, SHA-256 and PBKDF2 vectors; 48 files of 16 archives made with RAR 7.12 and 6.24 (-p, -hp, solid, stored, a Hangul password, `.partNN.rar` and `.rar/.r00` volumes, encrypted volumes) and libarchive's solid RAR 5 set, byte for byte as UnRAR 7.3.1 built from its source extracted them; wrong passwords refused at open (headers) or by `rubraview_rar_set_password` (files); a missing volume; a set opened from its second volume or its `.r01` through the page source. Opening a 120-file header-encrypted RAR 4 takes about 50 ms (one key, the salt repeats). Measured on the Windows 11 VM: a RAR 4 CBR (RAR 6.24 -ma4) read to page 20 as "RAR 4 (CBR)"; `locked pass.cbr` asked, "xy" said "wrong password" and asked again, "pass" showed four dots and opened page 1 of 20; its pages had thumbnails in the picker, the locked books none, and the later volumes were hidden ("3 other files hidden"); `locked names rar4 pass.cbr` (-hp) then opened with the remembered password, "Encrypted: yes, names too (AES)"; `split comic.part2.rar` opened as the three-volume set, page 20 at `End`; `split locked.part1.rar` (RAR 4, solid, -hp) opened with its password; `locked hangul.cbr` opened with 비밀 번호 typed through the Korean IME over RDP, and "비빌 번호" (a slip) was refused. The data RAR 1.5/2.0 encryption, a RAR set of more than 999 volumes, and editing inside archives are not in this.

## 2026-10-01: D-63 CBR / RAR comics, read by UnRAR converted to C

- Status: Accepted (owner 2026-09-30: "2-B도 참고하고 D도 참고하되, C로 컨버젼 해서 진행. 단 unrar이 라이선스가 우리가 갖다 쓸 수 있는 경우에만. 그렇지 않으면 그냥 외부 dll 갖다 쓸 수 있게." — plan `docs/plans/archive/2026-09-30-rar-reader.md`)
- Decision:
  - UnRAR's licence (read in `unrarsrc-7.3.1.tar.gz`'s `license.txt`) lets its source be used and modified in any software that handles RAR archives, but not to make a RAR-compatible archiver, provided its paragraph goes with it. We only read, so the conversion went ahead; no DLL fallback is needed.
  - `vendor/unrar-c/rar_unpack.c` is UnRAR 7.3.1's decompression (RAR 1.5, 2.x with audio, 3.x/4.x with the standard filters recognised as UnRAR 7 does, 5.0/7.0 with its filters) converted from C++ to C, line for line, single-threaded, one window up to 1 GB. RAR 3.x's PPMd goes through the vendored LZMA SDK's public-domain `Ppmd7` with 7-Zip's `Ppmd7aDec.c` (added to `vendor/lzma`). The licence paragraph is in `THIRD_PARTY_NOTICES.md`, `licences/unrar-LICENSE.txt` in the zip, the F1 help's notices and the source headers.
  - `src/core/rar.c` reads RAR 4 and RAR 5 headers (written for this project from UnRAR's and libarchive's readers), lists files, and decodes one: stored or compressed, a solid archive from the start of its chain or on from where the last read stopped, checked by the CRC the archive carries. Encrypted archives and files, and files split across volumes, are refused as such. Cost, cancel and progress match the 7z reader's, so a far page of a solid CBR is read off the main thread (D-49).
  - `.cbr` and `.rar` open like `.cbz` and `.cb7` (detected by signature, not extension), step to the next archive, show page thumbnails in the picker and are registered with the other file types; RAR 4's non-Unicode names go through the code-page rules ZIP's do.
- Consequences: `test_rar` checks 47 entries of 19 libarchive test archives (RAR 4: stored, LZ, the x86 filter, PPMd switching with LZ, several LZ blocks, Unicode names; RAR 5: stored, compressed, solid, the ARM filter, BLAKE2-hashed) byte for byte against bsdtar 3.7.7's extraction, reads a solid archive in any order, refuses passwords and split files, and survives 575 reads of randomly damaged packed data under ASan/UBSan. Measured on the Windows 11 VM with two 20-page RAR 5 comics made with RAR 7.12 (solid and not): the solid one opened on page 1 and `End` showed page 20; the other turned pages and its information said "RAR 5 (CBR)"; `Ctrl+]` stepped from one to the other; the picker inside a CBR showed its pages. Not measured on Windows: a RAR 4 comic (RAR 7 no longer writes RAR 4; covered by the host tests), a very large solid CBR's progress.

## 2026-09-30: D-62 The music features are connected

- Status: Accepted (owner 2026-09-30: "3. 전부 연결이요." — the analyser, the equaliser and night mode, ReplayGain, `.lrc` and `.cue`, all tested in core since M8 and never reaching the screen or the sound)
- Decision:
  - `src/core/audio_chain.c` (new, `test_audio_chain`) is what happens between the decoder and the device: the ten-band equaliser with its own state on each channel, a gain, night mode's -24 dB 4:1 compressor riding a level that rises in about 5 ms and falls in about 200 ms (the old per-sample gain would have distorted), and a tap keeping the newest 2048 samples mixed to one channel. The WASAPI output's thread runs it after the speed resampler and fills the tap; `rubraview_pal_audio_set_chain` and `rubraview_pal_audio_tap_read` are the viewer's side.
  - ReplayGain is read by `tags.c` from Vorbis comments (FLAC, Ogg, Opus) and ID3v2 `TXXX` frames (`REPLAYGAIN_TRACK/ALBUM_GAIN/PEAK`); Settings › Audio › Volume levelling (off, track, album) turns it into the chain's gain for the song playing, the peak keeping it from clipping.
  - Settings › Audio › Sound: Equaliser (flat, rock, pop, jazz, classical, bass, vocal, acoustic), Night mode, Analyser (off, spectrum, scope; spectrum by default). Toolbox buttons EQ / Night / Viz (EQ and Night on a film's too) and Playback › Sound cycle them.
  - A song's page: the analyser in a strip above its words, redrawn about thirty times a second while it plays (the paced redraw's own four a second otherwise); a `.lrc` of the same name as seven lines near the top, the current one larger, a click seeking to its line; a `.cue` of the same name (two tracks or more) naming the track playing, and previous / next seeking track to track inside the file — back to the start of the track first when more than 3 s in — before they change file.
- Consequences: measured on the Windows 11 VM with `rbmusic`: a `.lrc` beside `01 low.wav` showed its four lines with "Second line" current at about 1.8 s; a `.cue` beside `02 high.wav` named the playing track "Second half — Rubraview, no. 2/2"; the analyser's bars rose around the tone's frequency, the low tone's further left. Not measured by ear: the equaliser, night mode and volume levelling (host-tested: `test_audio_chain`, `test_tags`). The equaliser also colours a film's sound when chosen, which is what a player does. The chain is one set-up for every output, so two tracks crossfading share it. Not in this: a separate equaliser window with ten sliders (the presets are what the settings hold), embedded `SYLT`/`USLT` lyrics (only the `.lrc` file), and a `.cue` that names a different audio file than its own.

## 2026-09-30: D-61 Every setting on the window does something

- Status: Accepted (owner 2026-09-30: "1-A" — all 27 settings that were shown and saved but never read, wired)
- Decision: `settings_apply_rest` in `main.c`, called from `settings_took_effect` at start and on every change, reads them all. Live: frameless (a new `rubraview_pal_window_set_frameless`; our hover title bar stays away with Windows' own), title-bar trigger and hide delay, gutter, zoom step, wide-spread splitting and portrait collapse (layout options), touch tile size (`tile_metrics` scales tiles, anchors and toolbox buttons), colour management (`rubraview_pal_image_set_color_management`, the pages decoded again), accent (`COLOR_TILE_CURRENT` is now a variable; crimson, cobalt, emerald, amber, teal, purple), audio latency (`rubraview_pal_audio_set_latency_ms`, for the next output opened), read-ahead and keep-behind, and use keymap.ini (keys loaded again). Defaults that the reader also changes by hand — fit, layout, pixel grid, strip metadata on export — are applied at start and when the setting itself changes. At the next open: archive filename code page, memory cap, ComicInfo. At their own moment: on startup (no file given: blank, last file, or the picker in the last folder), reading history and the resume offer (off: opened where it stopped, without asking), the A-B step (Left/Right in the A-B box move the point in hand), wheel zoom during video (a plain wheel over a film zooms). Volume levelling waits for the music features.
- Consequences: two visible defaults change to what the settings always said: the accent is crimson (it was drawn blue), and a start with no file opens the picker in the last folder read. The audio device buffer is the setting's 40 ms instead of a fixed 100 ms. Measured on the Windows 11 VM with `frameless = false`, `accent = "cobalt"`, `tile_base_px = "96"`, `layout = "dual"`, `gutter_px = 32`, `startup = "last_file"` and no file given: the window had Windows' title bar with the icon, the last page read was reopened, the anchors and menu tiles were half as large again, and the pin was drawn in cobalt; the VM's `settings.ini` was put back after. Not measured one by one: the others, which are host-checked by `check-settings` (every wired setting is read) and built for Windows.

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
