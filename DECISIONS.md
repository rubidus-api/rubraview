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

## 2026-10-10: D-86 Explorer's right-click menu, registered by the viewer itself, extension by extension; printing

- Status: Accepted (owner 2026-10-10: "탐색기 우클릭 메뉴 동작 넣어주세요. 우선 압축파일들에 대해 내부 파일 보기, 다른 미디어 파일들에 대해 단일 열기와 현재 재생목록에 넣기, 배치 변환 처리하기, 인쇄 등 넣어줘. 여러개 동시에 선택해도 처리 가능하게요."; then "설치패키지와 우클릭 컨텍스트 메뉴 구현은 별개네요. 그냥 해당 프로젝트 참고해서 우클릭 메뉴랑 확장자 선택하여 등록 추진 바랍니다." — no installer; Rubrapack is the model, not a build tool here).
- Context: measured first on the VM — an unsigned identity package registers for a plain folder from an elevated process with no MSI, and is refused without an administrator (0x80073D2B). So the classic menu needs no rights and Windows 11's own menu needs them once.
- Decision:
  - **The menu** is one "Rubraview" item with six under it (`rubraview_shell_menu_items`): Open (pictures, video, music, comics, archives), Open these only and Add to the list (pictures, video, music), Browse the archive (comics, archives; one file), Convert... and Print... (pictures). English and Korean texts.
  - **Classic menu**: `Software\Classes\SystemFileAssociations\.ext\shell\Rubraview` (`SubCommands` empty, the items under its `shell`, `MultiSelectModel` Player / Single), HKCU for this user, HKLM for every user. It shows whoever the type's default program is.
  - **Windows 11 menu**: `rubraview_menu.dll` (IExplorerCommand in a surrogate; Rubrapack's `rubrapack_menu.c` adapted — MIT) and `rubraview_menu.msix` (a manifest naming each extension, three logos; made by `scripts/make-menu-package.py` for the version, a plain stored ZIP with a block map) ship in the zip beside `rubraview.exe`. "For every user" stages and provisions the package with the program's folder as its external location (Rubrapack's `rp_appx.h`, vendored unmodified) and writes what the class shows under `HKLM\Software\Rubraview\Menu\{CLSID}`; the DLL hides an item when a selected file's extension is not in its `On` list. Never for one user alone: Windows refuses it without an administrator. Names in `include/rubraview/menu_identity.h`.
  - **Choosing**: Settings gets a page Explorer — two switches (file types, menu), a switch for each of 29 extensions (the five plain archives get the menu only and are never a file type of ours), Register / Remove for this user and for every user. Registering makes the registry match the switches (ours is taken off what is off); Remove takes everything of ours. D-80's four kind switches are gone (a `shell.pictures` line in an old settings.ini is ignored). Command line: `--types=` takes extensions, kind words and `all`; `--no-types` / `--no-menu` leave a part as it is, `--remove-types` / `--remove-menu` take it back.
  - **One selection, one act**: the classic menu starts the program once for every selected file, the Windows 11 menu once with all of them (`--whole`; a selection too long for a command line goes as `--paths-from` a temporary file). A launch hands a packed request (verb, paths) to the open window, waiting up to 8 s for a window still being made; the window gathers requests of one verb until 0.5 s pass with none (`rubraview_shellreq_*`), then acts once. A plain launch is not waited on. The command line now takes any number of paths (it took 32 arguments and opened `argv[1]` whatever it was).
  - **Add to the list** makes one set of the pages being read and the new files, stays on its page, and puts a film back where it was. The set keeps the list's order setting. **Convert...** opens the batch panel on the chosen files (a list file handed to `--batch --paths-from`; output beside the first file). **Print** (`Ctrl+P`, File menu, the menu's item): `PrintDlgEx`, each picture on a sheet of its own, fitted with its proportions and turned a quarter when that fills more (`rubraview_print_fit`), see-through laid on white; a file that cannot be read takes no sheet.
- Consequences: the folder now holds a DLL of ours beside the program — the program does not load it (Explorer's surrogate does), and the single-file download still works alone, with the classic menu only. Moving the folder breaks the package's location: register again. Windows 11 fills a package's items a few seconds after the menu opens. With the classic menu the files arrive in the order their launches do, not Explorer's. `--register-shell` now ends with 3 or 4 when all was written but the Windows 11 menu (its files missing, or the package refused). Not done: a page inside an archive is not printed; no margins or layout options; printing holds the window while it runs; archives cannot be added to a list. VM 2026-10-10 (T135, T136): both menus with one file and several, each kind of item, an extension left out hidden, removal leaving no package, key or verb; a PDF of four pictures with the wide one turned and the see-through one on white. Not measured: the administrator question from the settings window (the elevated run itself was made from an elevated shell), another user's sign-in after provisioning, a selection long enough for the list file through the DLL, a film's place after Add to the list, a real printer.

## 2026-10-10: D-85 FultaArc at `4fb7c06`: ALZ and EGG open again

- Status: Accepted (owner 2026-10-10, on "re-vendor FultaArc to turn ALZ and EGG back on, and start RV-090?": "둘다 지금 진행해 주시고").
- Decision: `vendor/fultaarc` is replaced by FultaArc's commit `4fb7c06` (its D-017: ALZ and EGG with EGG's own method, from its clean room; the edit routes of RV-090), unmodified, with `src/alz`, `src/egg`, `src/azo` and `vendor/bzip2_alz` now taken and built as its own Makefile builds them. `.alz` and `.egg` join the archive filter (the picker, the folder's list, the neighbours of a book); the page source says which it opened (`RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ` / `_EGG`, the information window's Kind). They are not added to the file types the viewer registers (D-74 took `.zip` and `.alz` out; unchanged).
- Consequences: FultaArc's `4fb7c06` was a local commit there when taken, so Rubraview's public tree shows that ALZ / EGG code before FultaArc's own does. Host: `test_archives` reads an ALZ, an EGG in deflate, one in EGG's own method and a solid EGG back by size and CRC-32 (FultaArc's fixtures); the decoders' tests are FultaArc's. VM 2026-10-10: an `.alz` and an `.egg` of three pictures made there with ALZip open as books and turn pages.

## 2026-10-09: D-84 APNG plays; an animation is as long as its file

- Status: Accepted (owner 2026-10-09: "apng 구현 바람. 프레임 전체 384MB 및 8192 프레임 제한 이상 길이의 파일도 지원 바람.").
- Decision:
  - APNG: Windows' PNG codec reads such a file as its one still picture, so the file is walked chunk by chunk in the core (`apng.c`, host-tested) and each frame is written out again as a PNG of its own — the file's IHDR with the frame's size, the chunks before the picture's data (palette, transparency, gamma, profile), the frame's `fdAT` as `IDAT`, checks recomputed — which the same codec then reads. No PNG decoder was added. Place, delay, dispose and blend map onto D-82's canvas (dispose "previous" on the first frame is "background"; a delay of 10 ms or less is 100 ms as for the others). The file's own picture is a frame only when a `fcTL` precedes its `IDAT`. `.apng` joins the picture extensions (the open filter, the pictures kind of D-80, saving as PNG).
  - Frames: the reader owns the list, as long as the file (`rubraview_pal_anim_open` hands it out); the fixed 8192 list is left to TIFF pages and ICO sizes.
  - Past 384 MB: the picture as it stands after every so-manieth frame is kept (`rubraview_frame_keep_stride`, `rubraview_frame_canvas_seed`), and any frame is drawn from the nearest kept one before it — never from the file's start. What is kept is the picture with its see-through (it was the flattened one, which nothing could be drawn onto); it is flattened onto the window's colour when shown. A frame that is to be taken back off (restore) is not a place to go on from and is not kept.
- Consequences: an APNG's own loop count is not followed (`viewer.animation_end` decides, as for the others). A file over 1 GiB is not opened as an animation (the image reader's limit for any picture). The WebP codec hands frames over whole and reaches a far frame by decoding those before it, so a first jump far into a long WebP still waits on the codec; GIF and APNG frames are reached directly. The stride and the choice to keep unflattened pictures are the implementer's picks. VM 2026-10-09: an APNG with mixed dispose and blend plays, alone and from a CBZ, its last frame equal to ffmpeg's (laid together locally, the three samples' frames equal ffmpeg's except where a nearly transparent frame pixel lies over a nearly transparent one — ffmpeg gives black there, the viewer the spec's "over"); a 9000-frame APNG and GIF reach frames 8996 and 8998 (3:00.000); a 1920x1080 APNG of 400 frames (3.3 GB of pictures) plays, seeks and steps backward at once with 510 MB in use, frames 2, 186 and 398 as drawn; an ordinary PNG opens as before.

## 2026-10-09: D-83 The seek buttons: a tap is 5 s, held they offer 5 s to 5 min; Ctrl+Enter shows the file in Explorer

- Status: Accepted (owner 2026-10-09: "툴바에서 영상 재생중일 때 앞으로 뒤로가 30초가 기본인건 너무 많아요. 5초로 줄여주시고 누르고 있으면 드래그로 5초 10초 30초 1분 5분 버튼들 띄워주세요", then "-30s +30s 버튼은 없애고 ... 기본 뒤로 앞으로 버튼에 프레스 홀드 기능을 추가"; and "Ctrl+Enter 를 하면 현재 보고 있는 파일(압축파일 내라면 압축파일 전체)을 선택한 상태의 탐색기 창을 열게 해 주세요").
- Decision:
  - The toolbox's −30 s and +30 s buttons are gone (video and music). Back and forward seek 5 s on a tap, decided when the button is let go. Held 0.4 s, a row of five buttons opens over the button — 5 s, 10 s, 30 s, 1 min, 5 min, mirrored for back — the pointer is dragged onto one and let go; let go elsewhere, nothing happens. The same in the detached toolbox, inside its own window. `rubraview_seekfan_*` in `ui_box.c`. `Shift`+arrows and the edge buttons' second step keep their 30 s.
  - `reveal_in_explorer`, `Ctrl+Enter`, File menu "Show in Explorer": `SHOpenFolderAndSelectItems` on the file on screen, or on the archive for a page inside one.
- Consequences: the row's place (over the button, under it when there is no room), the 0.4 s and the mirroring are the implementer's picks. VM: tap, hold-and-choose, hold-and-let-go-elsewhere in the docked toolbox; Explorer with the archive picked out. Not run: the detached toolbox's row, a finger.

## 2026-10-09: D-82 An animated GIF or WebP is played as a film is, and laid together frame by frame

- Status: Accepted (owner 2026-10-09: "애니메이티드 webp는 동영상처럼 처리하게 해 주세요. 재생 가능하고 구간반복 가능하고 등등."; "애니메이티드 gif 도 색이 조금 이상하게 보여요").
- Context: measured on the VM — an animated WebP did not play at all (only the GIF's delay was read, so its frames had none and it was taken for a multi-page file); a GIF's frames were shown as the container holds them, each often a rectangle of the picture with see-through pixels, so sizes and colours jumped; each frame reopened and re-read the file; a file opened straight onto an animation did not start until a page was turned.
- Decision:
  - `rubraview_pal_anim_open/read/close`: the decoder stays open. WebP's time comes from `/ANMF/FrameDuration` (the Windows codec hands each frame over whole); a GIF's place, disposal and the whole picture's size from its metadata. A delay of 10 ms or less is shown as 100 ms, as browsers do.
  - `rubraview_frame_canvas_*` (core, host-tested) lays frames over one another — keep, clear, restore — starting from the nearest frame that needs none before it. Every finished frame is kept while all fit in 384 MB; a longer animation is drawn again when it goes back.
  - It is played as a film: a place in seconds, the toolbox's seek bar (click), ±5 s (arrows, buttons, D-83), frame steps, Stop, A-B (`[`, `]`, `\`, the typed box), speed on the film's ladder 0.25x–4x (kept for the session), the time in the title when paused, stepped or sought. `media_repeat_cycle` (`Ctrl+R`, the At the end button) cycles the animation's own end — `viewer.animation_end`: `loop` (default), `once`, `next` — apart from a film's, so a folder of GIFs does not start turning its own pages.
  - A tick's left-over time goes to the next frame (it was dropped, so every animation ran slow by up to a tick a frame), and the loop draws when a frame is up rather than on every pass.
  - Applies to every animated GIF and WebP alike (one path). APNG is not animated by Windows' PNG codec and stays a still.
- Consequences: found on the way and fixed — D-81's two tile sets reset each other on every frame in a two-page layout (`tiles_reset` for both where one was meant). Not done: sound (none exists), an animation past 8192 frames (the rest is cut), going back in an animation too long to keep costs a decode from its start, colour profiles in animations. VM: T131.

## 2026-10-09: D-81 Enlarging is our own: Lanczos-3, echoes removed, sharpened inside the neighbours' range

- Status: Accepted (owner 2026-10-09: "저해상도 이미지를 고해상도로 리사이징 할 때 되도록 잘 보이게 리사이징 모듈을 잘 선택하고 다듬어 주세요. 뭔가 고화질 공개된 리사이징 라이브러리 있는지 살펴보시고 벤더링 ... 라이선스는 mit", then "자체구현으로도 충분하면 자체구현도 좋습니다")
- Decision:
  - Looked at: AMD FidelityFX FSR 1 (EASU + RCAS, MIT) and classic kernels. Measured on four pictures (a drawn comic page with text, lines and screentone; three photographs), each reduced 1.5x to 4x with Lanczos and enlarged back, against the original: FSR's EASU was below plain Lanczos-3 in PSNR and SSIM on every picture and factor (it is made for 1.3-2x of rendered frames); Lanczos-3 with its results kept inside the four surrounding pixels and RCAS was better than Lanczos-3; the same with an unsharp mask kept inside the 3x3 neighbours' range instead of RCAS was as good or better, at every factor. That last one is taken, written here (`rubraview_enlarge_region`, `resample.c`): no third-party code, nothing vendored. Neural enlargers (Real-ESRGAN, waifu2x, Anime4K) were not taken: models and a GPU runtime many times the size of the viewer, and they draw detail that is not in the picture.
  - On screen it comes through the tile thread (D-40), which now also makes enlarged tiles: levels -1, -2, -3 are 2x, 4x and 8x, the least that is at least as sharp as the screen; a tile's picture pixels are read with five around them and the middle kept, which gives the same bytes as enlarging the whole. Direct2D's cubic draws first and the tiles come over it. There are two tile sets, one for each page of a pair.
  - Settings › Viewer › Enlarging a small picture: `sharp` (default) or `smooth`. Not for pixel art (`N`), a nearest or bilinear scaling filter, a film or an animation, the webtoon strip, or past 8x.
- Consequences: host test T129 (against a true drawing at 2x, 4x and 8x the error is below cubic's and Lanczos-3's — 187 against 356 and 381 at 2x — nothing outside the drawing's tones, tiles equal to the whole byte for byte). On the VM a 640x376 page at 2x: 23.15 dB against the original with `sharp`, 22.43 dB with `smooth`. What it cannot do: bring back detail a small picture never had.

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

## 2026-10-10: D-87 How a picture is printed is chosen in a panel before the system's dialog

- Status: Accepted (owner 2026-10-10: "인쇄 옵션을 인쇄시 고르게 하여 사용자가 회전 및 배율과 채우기 옵션을 다양하게 설정 가능하게 할것.").
- Decision:
  - `Ctrl+P` and Explorer's Print... open a panel of the viewer's own (the fourth beside Adjust, Export and Batch), not the system's dialog at once: Turn (auto / none / 90° right / 180° / 90° left), Size (fit / fill / stretch / actual size at 96 pixels an inch), Scale 10-400 % of that size, Place (centre, sides, corners), Margin 0-50 mm, 1 / 2 / 4 / 6 / 9 pictures a sheet. Its button leads to `PrintDlgExW` as before, where the printer, paper and copies stay.
  - The choices are settings (`print.*`, shown under Settings > Files > Printing), so they are kept and the next print starts from the last. The defaults print as D-86 did.
  - The arithmetic is the core's and host-tested: `rubraview_print_cell` (the sheet less the margin, cut into cells, the longer side taking the longer row) and `rubraview_print_place` (turn, size, scale, place; what would pass the cell is cut from the picture as a source rectangle, so nothing is drawn outside the cell). The Windows side turns the pixels (`turn_pixels`) and hands `StretchDIBits` the rows from the first printed one on, so a bitmap's row numbering is never in question.
- Alternatives: a page added to the system's dialog (`lphPropertyPages`) — a Win32 dialog template beside a program that draws everything itself, and not shown by every form of the system's dialog; options only in Settings — not "when printing".
- Consequences: the implementer's picks, not the owner's words: place, margin and pictures a sheet beside the three asked for; "actual size" as 96 pixels an inch (a file's own resolution is not read); the panel's choices step on a click, as the other panels' do. A page inside an archive is still not printed.

## 2026-10-10: D-88 A page inside an archive is deleted, renamed or saved by writing the archive again beside itself

- Status: Accepted (owner 2026-10-10, on the plan `2026-10-10-rv090-archive-edit`: "압축파일편집 시작. 휴지통에 넣게 하고 암호파일 거절하게 해서."; R149).
- Decision:
  - **What**: on a page inside an archive, `Delete` / `Shift+Delete` (asks `Y / N` — there is no bin inside an archive and no undo), `F2` (the page's name, in its folder there), and in the adjust panel "Save a copy" (a new entry `name_edit.ext` beside the page) and "Save over the page" (the entry replaced; another export format changes its extension). The scope question was not answered and was taken as recommended: all of them.
  - **Which way** (`rubraview_arcedit_route`): FultaArc's `fulta_arc_editable` says in place — a ZIP, a 7z with one file a folder — and the book keeps its format and name, untouched entries copied packed. Anything else (solid 7z, RAR, ALZ, EGG, a split set) is asked once per book and session, `Z` ZIP / `7` 7z, and written again whole; a comic's extension stays a comic's (`.cbr` -> `.cbz` / `.cb7`), a set's volume mark goes (`x.part1.rar` -> `x.zip`). An archive with any encrypted entry is refused, with its password or without.
  - **How** (`arcedit.c`, `rubraview_arcedit_run`, on a thread of its own): a second FultaArc handle on the file (the viewer's own keeps reading), the edit through `fulta_arc_edit_*`, written to `<archive>.rvnew` with `fulta_arc_edit_write_file`; then the new file is opened again and compared — the same files by name and size, checksums where both give one, the new bytes read back — and only then handed over. The main thread closes the book (Windows replaces nothing that is mapped), renames the new file over the old one (in place) or gives it its new name and sends every volume of the old one to the Recycle Bin (converted; a name already taken by another file becomes `name-1.ext`), and opens the book on the page. `Esc` stops the writing; a quit stops it too. While it runs no action but quit is taken and nothing is opened.
  - A converted archive stores Unicode names, so it takes each name as the viewer shows it (a ZIP's code-page names are read by the viewer, not by FultaArc); an in-place edit leaves stored names alone.
  - New PAL calls: `rubraview_pal_fs_replace` (`MoveFileExW` with replace) and `rubraview_pal_fs_free_bytes` (a 7z target needs about twice the unpacked size beside it: FultaArc spills every entry there first).
- Alternatives: writing in place inside the file (a failure half-way loses the book); deleting the converted original outright (the first wording of R149; the owner chose the bin); taking the password off an encrypted archive by writing it open (refused by the owner).
- Consequences: the implementer's picks, not the owner's words: level 1 for what is packed again (pictures do not pack further); `Shift+Delete` asks the same question as `Delete`; the last page of a book is not deleted; "Save over the page" and "Save a copy" both, because a loose file has only the copy. Found in FultaArc `4fb7c06` and worked round here, reported to it: a reader that says its file is 0 bytes long makes the ZIP writer free a buffer twice (the reader now says "unknown" for an empty file). Known and left: FultaArc writes a converted 7z as one solid run, so the next edit of that book converts it again (no question, but everything is packed again) — a ZIP is edited in place from then on. Undo does not reach into archives.

## 2026-10-10: D-89 What an archive becomes is chosen in a panel: format, method, level; an in-place edit can be taken back; archive pages print

- Status: Accepted (owner 2026-10-10: "7z 로 변환할 때 압축방식과 압축률 결정할 수 있는 화면이 있으면 좋겠네요 zip도 마찬가지", and of what 0.0.44 left undone: "안한 것도 같이 진행바람").
- Decision:
  - D-88's question "Z / 7 / Esc" is a panel (the fifth): Becomes (ZIP / 7z), Method (packed or stored), Level 1-9, a Write button (`Enter`). `Z` and `7` still pick the format. Closing it any way but by its button — `Esc`, a click outside, any other action — leaves the book. The choices are settings (`archive.convert_format`, `convert_method`, `convert_level`; Settings > Files), defaults ZIP, packed, 1 — what 0.0.44 did. The method and level also pack what an in-place edit writes anew.
  - The methods are the ones FultaArc writes: Store or Deflate in a ZIP, Copy or LZMA in a 7z (`fulta_arc_write_options_t.level`, 0 = stored). `rubraview_arcedit_job_t.level` carries it.
  - Undo: an in-place edit moves the old file to `<archive>.rvold` beside the book instead of replacing it; `Ctrl+Z` in that book puts it back. One step; the kept file is deleted at the next archive edit and when the program ends. A converted book is not undone (its original is in the Recycle Bin).
  - A page inside an archive is printed: laid in the temporary folder as a file for the print's duration, then removed.
- Alternatives: more methods (LZMA2, BZip2, PPMd, Deflate64) — FultaArc does not write them; undo by restoring from the Recycle Bin — Windows gives a program no way to (see `rubraview_pal_fs_restore_last_recycled`); keeping the old archive in the temporary folder — a copy across volumes for every edit of a large book.
- Consequences: while the kept file exists the book takes its size twice on disk; a program ended by force leaves the `.rvold` file (overwritten by the next edit of that book). The book the question is about is remembered, so an answer after another book was opened does nothing.

## 2026-10-11: D-90 An installer: an MSI made with rubrapack, the program registering itself

- Status: Accepted (owner 2026-10-11: "msi 또는 msix 로 설치 패키지도 만들어 주세요", after finding that Windows 11's menu needs the two files beside the program and an administrator). It adds to D-86, whose "no installer" it ends; the zip and the single executable stay.
- Decision:
  - `rubraview-v<version>.msi`, built by `scripts/package.sh` with rubrapack (the owner's tool; a released binary named by `RUBRAPACK` or on the PATH) from `resources/distribution/rubraview.toml` and the folder the zip is made of. Per machine, `Program Files\Rubraview`, a Start-menu shortcut, the folder page, English and Korean dialogs, "launch" on the last page. One upgrade code for good; a newer version replaces an older one, an older one is refused.
  - The registration is the program's own: the installer runs `rubraview.exe --register-shell --all-users --setup` after the files are in (and at repair), `--unregister-shell --all-users --setup` at removal. No `[menu.*]` or `[assoc.*]` table — two packages would answer for one menu. Settings > Explorer changes it afterwards as before.
  - `--setup` (`rubraview_shell_setup_exit`): an installer takes any exit code but 0 as failure and puts the files back, so a registration done without Windows 11's menu (3, 4) ends with 0, and a removal always does.
  - MSI, not MSIX: an MSIX of the program must be signed with a certificate whose subject is its publisher, and there is none. The menu's own small package stays unsigned, which Windows takes from an elevated installation.
- Consequences: an upgrade registers every extension again — what was switched off in Settings > Explorer for every user comes back (register again from there). What a user registered "for this user" is in that user's registry and is not removed with the program. The installer is not signed: SmartScreen says so. Without rubrapack `package.sh` makes the zip and the executable and says the `.msi` was not made. VM 2026-10-11 (T140): silent install, repair, upgrade 0.0.46 to 0.0.47, an older one refused (1603), removal leaving no folder, shortcut, package, key or verb; "Rubraview" in Windows 11's menu on a picture right after the install; the first dialog page in Korean. Not measured: the dialogs clicked through to the end, another user's sign-in, a Windows without the menu package.

## 2026-10-11: D-91 The reading order is part of the layout

- Status: Accepted (owner 2026-10-11: "레이아웃에 duel-r2l, book-r2l 모드 각자 duel과 book 뒤에 추가 ... comic 도 마찬가지 ... r2l 보다 좋은 접미어가 있으면 그걸 써 주세요", then "방향 따로 설정하는 아이콘 ... 위의 레이아웃 모드에 합치고 기존 설정은 없애주세요").
- Decision: `viewer.layout` has eight words — `single | dual | dual-rtl | book | book-rtl | webtoon | comic | comic-rtl` — and that order is the `B` round, the menu's and the settings choice's (`rubraview_layout_choice_t`; the engine keeps its five layouts and a direction, which the choice sets together). `rtl` is the suffix: it is what CSS, Unicode and ComicInfo's "RightToLeft" call it. Comic right to left shows a wide scan's right half first; clicks and swipes turn the other way in the three, as the order did. `toggle_reading_order`, its key `M`, its tile and its menu item are removed.
- Consequences: a settings.ini of 0.0.45 keeps its layout (the words are stored, not numbers) and loses a right-to-left order that was never stored. A keymap.ini or boxes.conf that names `toggle_reading_order` names an action that does nothing. One page and the webtoon strip have no right-to-left form. A ComicInfo `YesAndRightToLeft` still sets book and right to left for that book without changing the setting.

## 2026-10-11: D-92 The subtitle box holds its subtitle; its place and size are percents of the window

- Status: Accepted (owner 2026-10-11: "자막의 길이나 행이 많아져서 세로 높이가 늘어날 경우, 자막을 둘러싼 박스의 크기도 그에 맞게 늘어나서 자막 전체를 박스 안에 들어가게 ... 박스의 크기와 위치를 창 크기에 대한 퍼센트(박스의 중앙값 점이 창 전체의 몇퍼센트 위치에 있나)로 정할 수 있게 해서(그리고 추가로 왼쪽/가운데/오른쪽과 위/중간/가운데로 정렬로 위치 지정), 자막의 글꼴 크기가 자동조절되어서 박스안에 다 들어가게도"). It changes D-33, whose box was two lines tall whatever it held: a third line, or a line long enough to wrap, was drawn outside the pane.
- Decision:
  - `rubraview_subbox_layout` (pure, in `subbox.c`) lays the box out from a place (`video.subtitle_x`, `subtitle_y`: a point of the window in percents; `subtitle_align` left / center / right and `subtitle_valign` top / middle / bottom: which point of the box sits there, and how the text is aligned in it; `subtitle_width`, `subtitle_height`: percents) and the text's measured size (`rubraview_pal_render_measure_text_wrapped`: DirectWrite's layout at the box's inner width, so wrapped lines count).
  - Two ways to hold a subtitle that needs more than the box (`video.subtitle_fit`): `grow` (the default) — the box grows away from its anchored edge; `shrink` — the box keeps its size and the font comes down a twelfth at a time, to no less than ten points' worth, until the text fits. A font at its smallest that still does not fit grows the box after all. Either way the box stays inside the window and above the seek bar.
  - Defaults put the box where it was: centred, its bottom edge at 100 % (brought above the seek bar), 90 % wide, 12 % tall.
  - `M` and `R` on the box write these settings (whole percents). `R` no longer writes `subtitle_size`. A box placed before (layout.ini's `[subtitle_box]`) is turned into the settings once and the section is not written again.
- Alternatives: two position systems (a centre point, and separately an alignment) — one point with an alignment says both; the box always exactly as tall as its text — it would change height with every caption, so the configured height is the least; shrinking as the only answer — unreadable for a long caption in a small box.
- Consequences: the implementer's readings of the owner's words: "the centre point" is the anchor with centre / middle alignment, and the default down-alignment is bottom so that the box sits above the seek bar as before; the two "fit" answers are a choice, not both at once. Dragging moves in steps of one percent of the window. The outline of a shrunk font is thinned in proportion. The layout is kept until the caption, the window or a setting changes. VM 2026-10-11 (T144): five lines inside the box, a 240-character caption wrapped to five lines with the box grown, the same in a 10 % box shrunk to three smaller lines, a box at 5 % / 10 % anchored top-left with left-aligned text. Not measured: the M and R drags on screen (their arithmetic is tested), other DPI scales.

## 2026-10-11: D-93 The subtitle box may sit anywhere in the window; the 1:1 tile says the scale

- Status: Accepted (owner 2026-10-11: "자막이 영상 바깥으로 나갈 수 있게 해 주세요 ... 창의 위아래에 여백이 있을 거에요. 그쪽으로도 자막창이 이동할 수 있게", "화면에 배율 버튼 만들어 주세요. 현재 배율을 표시하고, 버튼을 누르면 100% 원래 크기에 화면 가운데 위치로 돌아오게요"). Supersedes the sentence of D-92 that kept the box above the seek bar.
- Decision:
  - The subtitle box's room is the whole window. The band along the bottom that it was kept out of was where the timeline strip had been (gone since D-54); only the filmstrip, while it is up, is still kept clear. The default place (bottom edge at 100 %) is now the window's own bottom edge.
  - The `actual_size` tile's caption is the scale of the picture on screen against its own pixels, in whole percents (`rubraview_scale_caption`; never under "1%"), for a picture, a film and the webtoon strip. A tap does what it did: actual size, zoom and pan put back, so the picture is at 100 % in the middle of the window. No new action.
  - The video toolbox gains that tile and Fit (the way back); the information line (`Shift+I`) gives the same figure, where it gave the zoom factor alone.
- Alternatives: a new `zoom_reset` action that puts the zoom factor back to 1 and shows that factor — it would read "100%" for a picture fitted to a quarter of its size; a new default of 94 % down to leave the box where it was — the owner asked for the margin.
- Consequences: the implementer's reading of "100% 원래 크기" is the picture's own pixels, not the fitted size (Fit, key `1`, is that). In the webtoon strip the tap leaves a page wider than the window at the window's width, so the tile may read under 100 % there. The subtitle box at its default place can lie under the open toolbox.

## 2026-10-11: D-94 A picture or a film can be looked at in another shape

- Status: Accepted (owner 2026-10-11: "비율조정 기능도 넣어주세요. 사용자 입력 비율로 재조정 혹은 ... 4:3이나 16:9 16:10 등 혹은 영화관 비율 등 ... A4 B4 US Letter 등 비율도").
- Decision:
  - View > Shape: Original, 4:3, 5:4, 3:2, 16:10, 16:9, 1.85:1, 21:9 (64:27), 2.39:1, 1:1, A4 / B4 (1 : root 2, every A and B sheet, upright), US Letter (8.5 : 11, upright), Custom (`viewer.aspect_width` : `viewer.aspect_height`, 235 : 100 as they come), and "On its side" which turns the chosen ratio over (9:16; paper, landscape). `Shift+A` (`aspect_cycle`) goes round them. One action a choice (`aspect_4_3` ...), so each can be given a key or a tile.
  - The file is not changed. The page's layout size is its own with the width made height x ratio (`rubraview_aspect_apply`; the height as seen is kept, turned a quarter or not), and the texture is drawn to the layout size by the per-axis scale a reduced texture already had. Layouts, fits, zoom, the strip and a film all follow from the layout size.
  - The choice holds for the session and for every file in it; it is not saved.
- Alternatives: a letterbox or a crop to the ratio — the owner asked for the size to be re-fitted ("크기를 재조정"), which is a stretch; keeping the width and changing the height — players keep the height; a text box for the ratio in the menu — the two numbers in Settings need no new control.
- Consequences: while the edit panel is open the picture is in its own shape (editing works in the file's pixels). The sharper tiles of a very large or enlarged picture (D-40, D-81) are not drawn while a shape is chosen. The scale the 1:1 button shows is against the stretched size. A DVD's or Blu-ray's picture subtitles stretch with the film.

## 2026-10-11: D-95 The information window says all a file tells of how it was made; a sound whose format changes while it is decoded is followed

- Status: Accepted (owner 2026-10-11: "최대한 자세히, 파일의 코덱이나 압축률, 각종 옵션이나 설정 등이 이미지 파일과 영상, 음성 파일 등에 대해 모두 가능한 한 모든 정보를 다 보여주게", "mkv 파일중에 음성이 제대로 제 속도로 나오지 않는 경우가 있었어요").
- Decision:
  - `rubraview_details_t` (core, `detail.c`): headings and label / value lines of fixed size. A media backend fills one while it opens the file (`rubraview_pal_media_details`): Media Foundation from the presentation's and each stream's native type (codec in words, profile and level, frame, pixel shape, frame rate, scan, bit rate, bits a pixel and the ratio against 8-bit 4:2:0, colour range / primaries / transfer / matrix, rotation; for sound the profile, framing, rate, channels with their speakers, bits, bit rate, the ratio against 16-bit PCM, block sizes), FFmpeg from the container and each stream (format, tags, profile, lossy or lossless, pixel and sample formats, field order, colour, delays, lengths, sizes, flags). Each stream also says what it was decoded as.
  - A picture's own account is read from its bytes (`rubraview_image_details`): JPEG (process, precision, chroma sampling, restart interval, JFIF, the blocks it carries, an estimate of the quality from its tables), PNG (depth, colour type, interlace, gamma, sRGB, chromaticities, pixel size, texts, animation, the deflate level and window), GIF (palette, frames, playing time, repeats), WebP (container flags, lossy or lossless, alpha, repeats) and BMP (header, compression, row order). Every picture gets the bits a pixel it is stored at and the ratio against its unpacked size.
  - A sound stream whose decoded format is not what the header said (HE-AAC written as its core rate, one channel that is two) is followed: Media Foundation's "media type changed" is read and the device opened again at the new rate and channels, in the state the old one was in; FFmpeg's resampler is rebuilt for the frame's own rate, channels and format.
- Alternatives: formatted values in the core with numbers passed across — the backends' tables are simple and each knows names the other does not; property-store metadata from Media Foundation — not done.
- Consequences: the sound fix is from reading the code, not from a file that showed it: no such file was at hand (see the test index). TIFF, HEIF, AVIF, JPEG XL and raw pictures get the ratio lines only. A list is at most 224 lines. After a format change the information's "Decoded as" line and the analyser keep the header's rate. The reopened device replaces the old one on the decode thread, as a track switch already did. Media Foundation's own figures are shown as given, right or not (it gives 12 frames a second for a 24 fps H.264 MKV).

## 2026-10-11: D-96 The two boxes start in opposite corners, stay inside the window, and keep their share of it

- Status: Accepted (owner 2026-10-11: "시작할 때 두 플로팅 박스가 하나는 왼쪽 위, 하나는 오른쪽 아래에 위치하게 하고, 이 두 플로팅 박스는 항상 윈도우 안에 ... 창 크기가 변해도 퍼센트 비율로 항상 창 내에서 같은 비율의 위치에", "Next를 두줄로 된 To Next로").
- Decision:
  - Each box's place is two shares, 0 to 1, of the room its anchor can move in (the window less the anchor less a margin of 8 px at each side): `rubraview_box_share_place` and `rubraview_box_share_of`. Every frame the anchors are put where the shares say, so a window of another size has them the same part of the way across and always whole inside. An anchor found somewhere else — dragged, docked, sent home — has its share taken, brought inside.
  - A new profile starts the menu box at (0, 0), the top-left corner, and the toolbox at (1, 1), the bottom-right. layout.ini keeps the shares (`toolbox_share_x` ...); the pixels an earlier version wrote are read once.
  - The toolbox while it is dragged, and while it is a window of its own, is left alone: dragging it past the edge is still how it becomes one.
  - The toolbox button for what the end of a file does reads "To" over "Next" when that is the next file ("Next" alone read as the next-file button). The menu's line and the on-screen message keep their words.
- Alternatives: percents of the window's whole width for the anchor's corner — a box at 100 % would be outside; clamping only on a resize — a share is what "the same part of the way" means.
- Consequences: a box dragged part of the way out is back inside when let go. The open grid was already shifted to stay inside (RFC-0002 §4).

## 2026-10-11: D-97 100 % is the size the fit option gives; real size is 0 %

- Status: Accepted (owner 2026-10-11: "피팅 옵션에 맞게 보여주는 것이 기준이 되는 크기 100% 입니다. 그리고 이와 별개로 0%는 real size를 뜻합니다. real size를 보여주는 버튼이나 기능이 있어야 합니다"; and again: "100%는 real size가 아니라 현재 fitting 옵션에 따라 현재 창에 배치한 그 크기가 기준 크기가 됩니다. real size는 0% 입니다"). Supersedes D-93's reading of the scale button.
- Decision:
  - The zoom is counted from the fitted picture: what the fit option in force (window, width, height, smart, stretch) shows in the window is 100 %. A new action `zoom_reset` puts the zoom back to that and the picture in the middle, the fit option kept; its toolbox tile's caption is the zoom now ("43%", "100%", "250%"). `Ctrl+0`, and View > "Zoom 100%".
  - Real size — one screen pixel a picture pixel, centred — is named 0 %: the `actual_size` button reads "0%" (View > Fit > "0% real size"; keys `0` and `4`), and while the picture is at real size the zoom tile reads "0%" too. Every toolbox with zoom buttons has both tiles, the video one too, with Fit.
  - The information line (`Shift+I`) gives the zoom, as it did before D-93.
- Alternatives: showing the scale against the file's pixels (D-93), or calling real size "1:1" or "100%" — the owner's words settle both.
- Consequences: `Ctrl+0` was a third key for real size and is now the zoom's. Zoomed from real size, the tile counts from it (110 %, 121 % ...), since real size is then what the fit option gives; only the unzoomed real size reads 0 %.

## 2026-10-11: D-98 A layout for looking at pictures leaves films, sounds and moving pictures out

- Status: Accepted (owner 2026-10-11: "dual계열, 책 계열, comic 계열, 웹툰계열 뷰와 같이 이미지를 보는 레이아웃 일 때는 음악이나 애니메이티드 이미지, 동영상은 화면에 보여주지 말고 그 다음 이미지를 보여주는 것으로 해 주세요. 화면에 두개의 동영상을 보여주거나 할 필요는 없으니까요").
- Decision:
  - In every layout but Single — dual, book, comic (each also right to left) and the webtoon strip — a page that is a film, a sound (by its extension) or a picture that moves is not laid out: `rubraview_page_info_t.skip`, which `rubraview_layout_compute` leaves out of the spreads, so the picture after it takes its place and pairs with the one before. A narrow window that shows a dual layout a page at a time leaves them out too. The strip keeps a place for every page, so there the page has no height, is not drawn and is stepped over.
  - A picture that moves is known when it is read: `rubraview_image_is_animated` looks at the bytes (a GIF's second frame, a WebP's animation flag, a PNG's `acTL` before its data), for `.gif`, `.webp`, `.png` and `.apng` only; the layout is made again when one is found.
  - The page the reader chose by name — opened, or picked in the file list, the playlist, the picker, the mini player or on the page bar — is shown whatever the layout, alone (`layout_exempt`), until another page is turned to. Otherwise opening a film while the layout is dual would show something else.
  - With nothing but such pages, all are shown. In Single nothing is left out.
- Alternatives: showing a moving picture's first frame as a still — the owner asked for the next picture; reading every GIF and PNG of a folder when it is opened to know beforehand — thousands of files read for nothing.
- Consequences: a moving picture may show for a frame before it is found and left out. The position in the title still counts every file ("3/40"). The next-file end of a film goes on to the next film as before, each being the chosen page in turn.

## 2026-10-11: D-99 Several viewers at once: a window each, and state files that survive it

- Status: Accepted (owner 2026-10-11: "한번에 여러개의 루브라뷰 프로그램이 뜰 수 있게 해 주시고, 설정파일 한꺼번에 접근하거나 하는 것을 방지하는 락이나 동기화 메커니즘 등을 갖춰주세요. 임시파일 같은 것도 겹치지 않게 조심해 주시고요").
- Decision:
  - `general.single_instance` ("Reuse the open window") is off as it comes: each launch is a window of its own. On, a launch hands its file to the open window as before. Explorer's right-click actions go to the open window either way (they act on a selection as one); `--new-instance` is a new window either way.
  - A state file is written to a file of the process's own beside it (`<name>.<pid>-<tick>.tmp`) and exchanged for the real one in one step (`rubraview_pal_fs_write_file_atomic`: `MoveFileEx` with replace, retried for 200 ms, then written in place as before): a reader sees the old file or the new one, never half. settings.ini, keymap.ini, layout.ini, history.ini and favorites.ini are written so.
  - One named mutex for the user (`Rubraview_Config_Mutex`, global, or local to the session where that is refused) is held while a viewer reads a state file, puts its changes in and writes it back (`rubraview_pal_config_lock`, 2 s; past that it writes anyway — the worst is one change lost).
  - What is put in: settings — only the keys this viewer changed since it loaded or last saved, over the file as it is now (`rubraview_settings_merge_changed`); the history — the file's entries and this viewer's together, each book at its newer place (`rubraview_history_merge`); a favourite added or removed — into the list as the file has it now. keymap.ini, layout.ini, and a favourites rename or reorder are the last writer's whole.
  - Temporary files were already named by process and moment (`rubraview-<what>-<pid>-<tick>-...` in the user's temporary folder; a drop's folder by process): nothing changed there.
- Alternatives: keeping one window as the default and leaving the switch to the reader — the owner asked for several at once; file locks (`LockFileEx`) on each file — a mutex with an exchanged file needs no lock a crash could leave; reloading other viewers' changes live — not asked, and a viewer's open settings window would change under the reader's hand.
- Consequences: a viewer does not see what another changed until it is started again. Several files opened together from Explorer with Enter are now a window each (the right-click "Open these only" is one window). The exchange replaces the file, so a link or special permissions on a state file are not kept. The start-up check reads the setting through the loader, so no settings file means a window each. It also reads the file the viewer itself uses (beside the executable when portable, under AppData otherwise): it had looked in the working folder, so an installed viewer was one window whatever the setting said.

## 2026-10-11: D-100 A file from Explorer joins the open window's list; a window of its own is asked for

- Context: owner, 2026-10-11: "탐색기에서 이미지를 열면 현재 재생목록 맨끝에 이미지가 추가되면서 새 파일로 열리게 합시다. 맨 처음 열릴때만 같은 폴더 파일들을 재생목록에 같이 넣고 열리게. 우클릭 매뉴에 새 창에서 열기를 넣고, 대신 여러 인스턴스로 실행은 가능하게 하자. 그리고 가능하다면 이런 동작들 세세하게 옵션 설정". D-99 had made every launch a window of its own.
- Decision:
  - `general.single_instance` ("Reuse the open window") is on as it comes again: a file opened from Explorer goes to the Rubraview window that is in front.
  - What that window does with it is `general.explorer_open`: `add` (as it comes) — with files being read, the file goes to the end of their list and is turned to; `folder` — opened in place of what was open, with its folder, as before D-100; `only` — opened in place of it, alone. A book, a folder or a playlist is never added to a list: it is opened in place.
  - The first file a window is given brings its folder (`general.first_with_folder`, on); off, it is alone and what comes after joins it.
  - `general.added_order`: `end` (as it comes) keeps the list as it stands and puts the file after it (`RUBRAVIEW_SORT_AS_GIVEN`); `sorted` sorts the whole list as Settings > Files says. `general.added_shows` (on): the added file is turned to; off, the reader stays on the page. Several files arriving within half a second (one launch a file, from Explorer) turn to the first only.
  - The right-click menu's "Add to the list" follows `added_order` too and stays on the page, as before. "Open" is the double click.
  - A window of its own: the menu's new "Open in a new window" (`--new-window`), `--new-instance`, or "Reuse the open window" switched off. Several can run; D-99's exchange and merging of the state files is what keeps them apart, and nothing new is shared.
- Alternatives: asking the window used last rather than the one in front — a record shared between viewers, for a difference seldom seen; gathering a double-clicked selection before acting — every double click would wait half a second.
- Consequences: a window that restored the last folder when it started has files open, so the next file from Explorer joins that list. A file already in the list is turned to, not added twice. A list that a file was added to is a set of files: it is no longer the folder (the list window says "Playlist"). With the classic menu, "Open in a new window" on several files is a window each; Windows 11's menu gives them to one. A viewer registered before this version shows the new item after Explorer is registered again (Settings > Explorer, or the installer).

## 2026-10-11: D-101 What RFC-0001 left for viewing: playlists opened, subfolders, filters, an order from the command line, a choice of transition, whole-step zoom

- Context: owner, 2026-10-11, on RFC-0004 §1 (what RFC-0001 describes and 0.0.51 did not do): build all six.
- Decision:
  - A playlist file (`.m3u8`, `.m3u`, `.rvlist`) opens as its files in its order (`open_playlist`); a path that is not whole is from the playlist's folder; a file that is not there is counted and left out. The picker lists playlist files.
  - `files.subfolders` (off), or `--recursive` without `--batch` for one run: a folder's files and those of the folders under it are one sequence, eight folders deep at most; by name they go folder by folder (`RUBRAVIEW_SORT_PATH_NATURAL`).
  - `files.show` (all | pictures | films | music) and `files.pattern` (name patterns, `;` between them) say what a folder lists. The file opened by name is listed whatever they say; when nothing passes, the whole folder is listed and the viewer says so. A set of files, a playlist and a book are not filtered.
  - `files.sort_mode` gains `random`: another order each time a folder is opened. `--sort=natural|lexical|modified|created|size|random` and `--descending` / `--ascending` set the order for one run, over the settings.
  - Changing the order, the filters or the subfolder switch lists the open folder again, on the same file.
  - `viewer.transition` (cut | fade | slide | zoom, fade as before): how a turned page arrives (`rubraview_transition_poses`). A slide comes from the right going forward, from the left going back. A slide show quicker than half a second cuts.
  - `viewer.whole_zoom` (off): a picture shown larger than it is takes a whole number of screen pixels a pixel — the next one down from what the fit and the zoom ask. A film is left alone; reducing is not changed.
- Alternatives: menu items and keys for the filters and the subfolder switch — they are settings a reader sets once, and every action costs a tile count in two documents; a seed kept for the random order so that going back retraces it — a folder listed again (after a delete) is shuffled again.
- Consequences: the title's count is of what is listed, so a filter changes it. With subfolders on, a folder with very many files under it takes as long to open as listing them all does (200,000 at most). The zoom button still shows the zoom asked for, not the whole step drawn. The slide and the zoom transitions were checked by their numbers, not caught on screen.

## 2026-10-11: D-102 What RFC-0001 left for input and settings

- Context: owner, 2026-10-11, on RFC-0004 §2: build all of it.
- Decision:
  - `media_seek_forward_fine` / `media_seek_back_fine` (`Ctrl+Shift+Right` / `Left`): one second, in a film, a song and an animation.
  - A pan gesture that ends having gone sideways (twice as far across as down, 64 px or more) over a fitted page turns the page, the way reading goes (`RUBRAVIEW_WINDOW_EVENT_GESTURE_END`, `rubraview_pointer_swipe`); the page is put back where it was. Not in the strip, not zoomed in, not in the picker.
  - `video.subtitle_font` (a family name, typed), `video.subtitle_color` (white, yellow, cyan, green, pink): `rubraview_pal_render_set_text_family` is set while a subtitle is measured and drawn and taken back after.
  - `video.audio_language`, `video.subtitle_language` (typed: `kor`, `eng`, `ko` ...): the track `rubraview_tracks_choose` finds in that language is the one a film opens with.
  - `viewer.background` (dark, black, grey, white): what is behind and between pages.
  - Settings > Cache: "In use now" says megabytes of the cap; "Clear the picker's thumbnails"; "Forget every reading place" (the history here and its file, under the lock).
  - `general.native_dialog`: `Ctrl+O` opens Windows' own dialog in place of the picker.
- Alternatives: a font picker — the settings window is text; a typed family is what it can hold. A colour picker likewise: five colours.
- Consequences: a setting typed in the Video and Files pages is text, not a folder (`settings_path_commit`). A family the machine lacks is drawn in DirectWrite's fallback, with no word about it. "Forget every reading place" does not reach a viewer that is open elsewhere: it writes its own places back when it closes. Whether Windows sends a one-finger sideways drag as a pan gesture (it may arrive as a mouse drag) was not seen: no touch screen.

## 2026-10-11: D-103 What RFC-0001 left for editing and export

- Context: owner, 2026-10-11, on RFC-0004 §3: build all of it (WebP is RV-109, apart: it brings third-party code).
- Decision:
  - The adjust panel has two pages — the tones; sharpness, cut and size — with a button to turn between them, so that it is never taller than a small window (`panel_build_edit`; the rows are made from the session as it stands).
  - Page two: Blur and its kind (Gaussian, box), Sharpen and its kind (unsharp mask with a radius and a threshold, or Laplacian: `rubraview_filter_laplacian_sharpen`), Crop ratio, "Trim plain margins" (`rubraview_filter_autotrim_rect` on the preview's copy: white margins, else black, marked as the cut for the reader to see before saving), Resize % (10 to 500, of what the cut leaves).
  - The preview is drawn again when a blur or a sharpening changes, with the radius scaled to the preview's size. It had followed the tones only.
  - Export: "PNG colours" (32, 24, 256 colours, grey) and "Progressive JPEG"; `--png-depth=rgba|rgb|palette|gray` and `--progressive` for `--batch`. Windows' JPEG writer has no progressive mode: the file it writes is rewritten by the vendored libjpeg-turbo, coefficients untouched (`rubraview_jpegtran_progressive`). The option had only changed the chroma sampling; that is gone.
  - `copy_frame` (`Ctrl+C`) and `save_frame` (`Ctrl+Shift+E`): the picture on screen of a film or an animation, to the clipboard or to `<name>_MM-SS-mmm.png` beside the file. A frame on the graphics card is read back (`rubraview_pal_texture_read_video`).
  - A panel's choice says its word (it showed a number in brackets).
- Found on the way: the Export panel began at quality 0 — `export_options` was never given its defaults. It is now.
- Alternatives: one long panel that scrolls — the panel has no scrolling and the curve box sits beside it; a cut made at once by Trim — the reader could not see what would go.
- Consequences: a frame in an archive can be copied, not saved (it has no folder). `--out=` still wants a folder that exists. The second page was seen on the VM with its values; a blur and a Laplacian sharpening were not looked at on a photograph.

## 2026-10-12: D-104 The recent files, and folders that are not written down

- Context: owner, 2026-10-12: "최근에 연 파일 목록과 그것의 초기화 기능을 File 메뉴에 ... 최근에 연 파일 목록에 저장하지 않을 폴더(그 폴더의 파일과 그 하위 폴더의 파일 전부는 저장되지 아니한다)들을 추가하거나 삭제". File > Recent existed and was made from the reading history, which is keyed by the folder a picture is in and holds a book only once a page of it was read: it showed folders' names, not the files opened.
- Decision:
  - `recent.ini`, beside the history: the path the reader opened by name (a file, a book, a folder, a playlist) and when. Thirty are kept; File > Recent shows the newest ten (`rubraview_recent_*` in `history.c`). Of a set of files, the one it opened on is the one written.
  - It is written at each open, under the lock every viewer takes, merged with what the file holds: every viewer's menu shows what any of them opened.
  - File > Recent > "Clear this list" asks first, as Delete does (a second tap within five seconds). An emptied list carries the time it was emptied (`[cleared] time`); an entry no newer is dropped wherever two lists are put together, so a viewer that still holds the old entries does not bring them back.
  - Settings > Files: "Keep the list" (`recent.keep`), and eight folders (`recent.skip_dir_1` .. `_8`) — a file in one, or anywhere under one, is not written to the recent list, and its reading place is not written to the history (`rubraview_path_is_within`: at a separator, letters' case ignored). Entries already there go at the next write. A folder is added by typing it in a free row and taken off with Delete.
- Alternatives: a list of any length in the settings window — it has no such row; the nine curation folders are the precedent. Leaving the reading place of a kept-out folder in the history — the same file would still be named in a file on disk.
- Consequences: the reading history no longer feeds the menu; "Forget every reading place" (Cache) and "Clear this list" are two things. With "Keep the list" off nothing new is written; what is listed stays until cleared.

## 2026-10-12: D-105 Every open viewer has the same settings, at once

- Context: owner, 2026-10-12: "여러 인스턴스가 떠 있을 때, 그들은 모두 동일 설정을 공유하고 하나의 루브라뷰의 설정이 변하면 다른 인스턴스에도 즉각적으로 영향이". Since D-99 the viewers shared the file safely, but one read it only when its settings window opened, and wrote it only as that window closed or the viewer quit.
- Decision:
  - A change is written 0.3 s after the last of a run of changes (`settings_share`), wherever it was made — the settings window, a key, the wheel on the volume — as D-99 writes: only what this viewer changed, over the file as it is, under the lock.
  - The writer posts a message to every other viewer's main window (`rubraview_pal_instance_tell_config_changed`: the windows of our class in other processes). A viewer told so reads the file, keeps on top of it what it changed itself and has not written yet, and lets what differs take effect (`settings_follow`). What it read becomes its baseline, so it writes nothing back: no echo.
  - Revert in the settings window goes back to the settings as they were when that window opened (`settings_opened`), with what other viewers changed since left in.
  - keymap.ini is still written as the settings window closes; the others are told and read it then, unless their own settings window is open.
- Alternatives: watching the folder — a message is immediate and needs no thread; a broadcast to every window on the desktop — only our own windows are sent it.
- Consequences: two viewers that use different files (one portable, one not) tell each other and find nothing changed. The boxes' places (layout.ini) stay each window's own until it closes. A setting changed in two viewers within the same 0.3 s: the later write wins for that setting. Each write a viewer follows is read into its working memory, which is not given back (a new block is taken when one fills): a long session beside a window whose volume is turned often grows by a few kilobytes a write. The keys are read again only when the keys were written. With settings written as they change, closing the settings window seldom says "settings saved" any more.

## 2026-10-12: D-106 WebP is written by the vendored libwebp, animations too

- Context: RV-109; owner, 2026-10-11 ("WebP는 libwebp 포함") and 2026-10-12 ("libwebp는 벤더링 바랍니다. 움직이는 webp 쓰기와 다른 소스, 예를 들면 애니메이티드 gif나 png 등으로부터의 변환도 ... 어렵다면 ffmpeg가 있을 때만"). Windows has no WebP encoder; for WebP the save path asked WIC for its JPEG XR container (what that wrote was not looked at before it was replaced).
- Decision:
  - libwebp 1.6.0 (the latest release; BSD-3-Clause with Google's patent grant; ledger R015) is vendored unmodified at `vendor/libwebp/`: libwebp, libwebpmux, libsharpyuv. Built by the Makefile into a static library with no `config.h` — C and SSE2, no threads.
  - `src/core/webp_write.c` is the one file that calls it: a still picture (`rubraview_webp_write`), or an animation a frame at a time (`rubraview_webp_anim_*`, over `WebPAnimEncoder`). Lossy with a quality, or lossless; effort 0..6. Premultiplied and RGBA buffers are taken as they are.
  - `rubraview_pal_image_save` writes WebP through it. `rubraview_pal_image_save_animation_webp` reads an animated GIF, WebP or PNG with the viewer's own reader and canvas (so every frame is the whole picture as it is shown) and writes an animated WebP with each frame's time. Export / Save a copy use it when the picture was not edited; `--batch --format=webp` when no turn, resize or adjustment was asked for. Otherwise one picture is written, as for every other format.
  - Export has "WebP lossless" and "WebP effort"; `--batch` has `--lossless` and `--effort=0..6`.
  - ffmpeg is not needed for any of it, so the fallback the owner allowed is not used.
- Found on the way: a frame saved with `Ctrl+Shift+E` (0.0.54) had red and blue exchanged — the writer took every buffer as RGBA whatever it was marked. It reads the mark now.
- Alternatives: the encoder alone — `anim_encode.c` calls the decoder, so the animation encoder cannot be had without it; ffmpeg's libwebp wrapper — only when the reader has put FFmpeg beside the program, and built with libwebp.
- Consequences: `rubraview.exe` grew from 4,032,676 to 4,683,005 bytes (about 650 KB; the estimate was 300 to 400 KB, made before the decoder was known to be needed). The viewer still reads WebP through Windows. A written animation loops for ever: the readers do not carry a source's loop count. An animation that is edited, resized or turned is written as one picture. A picture wider or taller than 16383 cannot be a WebP and is refused.

## 2026-10-12: D-109 One zoom tile that goes its round; zoom by tens; a click pauses a film and the wheel is its volume; a drag at any zoom

- Status: Accepted (owner, 2026-10-12: "배율 퍼센트 아이콘이 2개가 있어요. 1개로 줄여주세요. 동영상/음악 재생시에 화면 좌클릭은 일시정지/재생으로 처리해 주시고, 화면 위에서 스크롤 휠을 돌리면 소리 크기 조절하게 해 주세요. 화면100% 상태에서도 드래그로 화면 이동가능하게 해 주세요. 줌 버튼을 클릭하면 0%(realsize)->20% 40% ... 200% 다시 0%로... 순환되게 해 주세요. 줌 버튼 위에서 휠을 돌리면 10% 단위로 ... 화면 위에서 Ctrl+휠을 돌려도 그렇게")
- Decision:
  - The toolboxes have one percent tile, `zoom_cycle`, in place of `zoom_reset` and `actual_size`. Its caption is the zoom now ("0%" at real size); a tap goes 0 %, 20 %, .. 200 %, 0 %. From a zoom between, the next multiple of 20; past 200, real size. The actions `zoom_reset` (`Ctrl+0`, the menu) and `actual_size` (`0`, `4`, the menu) stay.
  - A zoom step is so many percent of the fitted size, to the next multiple (`viewer.zoom_step_percent`, 10 as it comes), not a factor: `Ctrl` + wheel, Zoom+ / Zoom- (tiles and keys), and the plain wheel over a zoom tile. The least is one step, the most 6400 %. Each says the percent it reached.
  - Real size is beside the scale (D-97), so a step taken in the real-size fit first goes back to the fit it was entered from (the window fit when that is not known), keeping the size on screen, and steps from there.
  - A left press on the picture is decided when it is let go: moved more than 4 px (by the display's scale) it was a drag and the picture followed, at any zoom; otherwise a click. A click over a film or music pauses and plays. Over a picture it is what it was (page zones, the boxes), and nothing on a picture larger than the window.
  - The plain wheel is the volume while a film or music plays, 5 a notch. The setting `video.video_wheel_zoom` is replaced by `video.wheel_volume` (on as it comes); off, the wheel zooms a film as it did. A new name, so that a settings file holding the old one does not keep the old behaviour.
  - 0.0.73 (owner, 2026-10-14, asking for the click again): two presses did not pause. One that moved more than 4 px before it was let go was a drag — seen in the VM with 8 px — so over a film the drag begins past 12 px, and over a song, which has nothing to drag, never. And a press on the band of a song's words was taken by it also where the words have no times; now only a timed line takes it (the song goes there, owner 2026-09-30).
- Alternatives: keeping the factor step — the owner asked for tens; a click acting on the press — then a drag at 100 % would turn the page first.
- Consequences: a click acts a moment later than it did (on release). Zoom+ / Zoom- changed with the wheel, though only the wheel was named: one kind of step. A drag can put the picture wholly outside the window (`Ctrl+0` brings it back). With "Enlarge by whole steps only" on, the steps are still rounded down to whole multiples when drawn. Seen on the VM with the pointer driven by a script (see T161); not seen: a touch screen's tap and drag, a step out of real size by the wheel, the setting turned off.

## 2026-10-12: D-108 JPEG XL is read through the user's FFmpeg; a picture in an archive takes the same road as one in a folder

- Status: Accepted (inside RV-110's confirmed plan; owner, 2026-10-12: "다음 진행" to the order proposed — the FFmpeg road first, a C decoder to be assessed and asked about afterwards)
- Context: D-107 left JPEG XL to a WIC codec that most Windows do not have. libjxl is C++ and is not to be in the build (the owner). The FFmpeg DLLs a user may put beside the program (D-8, D-26) are built by others, and the usual builds have libjxl in them.
- Decision:
  - When Windows' codecs refuse a file whose first bytes are JPEG XL's (`rubraview_image_is_jxl`: the bare codestream's FF 0A or the box container's signature), the FFmpeg beside the program reads it (`rubraview_pal_ffmpeg_jxl_decode`): from memory through a custom `AVIOContext`, never by a file name, so the file stays unlocked and an archive's page is read the same way; the first frame, as R, G, B, A of 8 bits, upright (libjxl applies the orientation itself). It is handed to WIC as the in-memory TIFF AVIF already uses (`tiff_room` / `tiff_decoder`, shared now).
  - Only for that signature. FFmpeg guesses at whatever it is given; a damaged file of another kind is not handed to that guess.
  - Order for a `.jxl`: Windows' codec, then FFmpeg. For an AVIF nothing changed (ours, then Windows'); AVIF is not sent to FFmpeg — the built-in decoder made that road pointless, so that part of the plan is dropped.
  - The FFmpeg loader is entered once whichever thread asks first (`InitOnceExecuteOnce`): pictures are read ahead on other threads than the film's.
  - The notice for a `.jxl` that stays empty says what is known: no decoder in Windows and no FFmpeg beside the program; an FFmpeg that has no JPEG XL decoder in it; or that the picture could not be read. `--probe-media` prints whether the FFmpeg found has the decoder.
  - A picture inside an archive is opened by the same function as one in a folder (`rubraview_pal_image_load_texture_from_memory` goes through `decoder_for_memory`). Until now an archive's page went to Windows' codecs alone, so an AVIF in a ZIP was shown by Windows' codec (which drops the alpha: D-107's measurement) and, by the code, would not have been shown on a Windows without it — that was not seen, the VM has the codec. Found on the VM while testing a JPEG XL in a ZIP; afterwards an AVIF in a ZIP was seen with its alpha.
- Alternatives: vendoring libjxl — C++; a C decoder (J40 is the one known, experimental by its own description) — not assessed yet, and to be put to the owner before any use; opening by file name — it would lock the file and leave archives out.
- Consequences: nothing of FFmpeg or libjxl is compiled or shipped; rubraview's build is still C alone. A `.jxl` opens only where the user has Windows' codec or FFmpeg's DLLs with libjxl. An animated JPEG XL is a still picture (its first frame). A JPEG XL's colour profile is not applied (as for AVIF), and one of more than 8 bits is shown as 8. Still asking Windows alone: thumbnails of archive pages and the tiles of a picture too large for the graphics card (each tile would decode the whole picture again through this road). Measured on the VM with BtbN's `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0` DLLs: lossless 8-bit, 16-bit, boxed and alpha files equal to their sources pixel for pixel; a lossy one within 1 of 255 of libjxl on the host. Not seen: an animated JPEG XL, a turned one, an FFmpeg without libjxl.

## 2026-10-12: D-107 AVIF is read by a built-in decoder; JPEG XL is listed and left to Windows

- Context: RV-110; owner, 2026-10-12: "다음 프로젝트로 avif와 jxl 지원을 준비해 주세요", then on the plan's questions "다 추천대로 하지만 c++은 넣지 않습니다. nasm도요." Measured first on the VM (Windows 11 25H2; this VM has the Store's HEIF, AV1, VP9, WebP, HEVC and Web Media extensions installed — what another Windows has was not measured): AVIF opened through Windows — one picture of an animation, and (found by comparing with Pillow) with the alpha thrown away and the chroma's pixels doubled; JPEG XL did not open; neither was listed by the viewer at all.
- Decision:
  - `.avif` and `.jxl` are pictures everywhere: the folder list, the picker, `--batch`, Settings > Explorer (`shell.ext_avif` on, `shell.ext_jxl` off as it comes — it cannot be opened on most machines).
  - AVIF is read by vendored code: libavif 1.4.2 (R017) over dav1d 1.5.4 (R016), both unmodified, dav1d from its C code alone (no assembly: the owner's decision). `src/core/avif_read.c` is the one file that calls them: a still or a sequence, 8 to 12 bits given as 8, alpha, the container's rotation and mirroring as an Exif orientation.
  - The built-in decoder is tried first, Windows' codec only when ours refuses a file: ours keeps the alpha, agrees with Pillow within 4 of 255 where Windows' differed by up to 91 at edges, and took the same time on a 3840x2160 picture (about 2.0 s for the whole `--batch` run either way, on the VM). `RUBRAVIEW_AVIF_OWN=0` puts Windows first, for comparing.
  - In the Windows layer the decoded picture is handed to WIC as an uncompressed TIFF in memory (`decoder_from_avif`), so every road a picture takes — texture, export, batch, description — is the one it was; an animated AVIF goes through the animation reader like a GIF.
  - JPEG XL is read through a WIC codec for it where one is installed (Microsoft is said to offer a JPEG XL Image Extension; the VM has none and this was not seen). libjxl is C++ and is not vendored (the owner's decision). A page that could not be read now says so once ("this picture could not be read") instead of staying empty; for JPEG XL, that this Windows has no decoder for it.
  - Nothing new is written: no AVIF, no JPEG XL.
- Found on the way (the owner's "webm, flac, aac, tiff 등", measured without FFmpeg): FLAC (16 and 24 bit, 5.1), AAC (ADTS, M4A), Opus, Vorbis and WebM with VP8, VP9 and AV1 play; every TIFF kind tried opens except BigTIFF. **A 10-bit film was shown as garbage on the software path** (VP9 profile 2; HEVC and AV1 10-bit go the same way): the source reader's old video processor does not take P010. It is asked for the newer one now (`MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` without a D3D manager too).
- Alternatives: Windows' codec first — it loses the alpha; libgav1 — C++; dav1d with assembly — needs nasm.
- Consequences: `rubraview.exe` grew from 4,683,517 to about 5.7 MB (+1.0 MB). Where the built-in road is not taken: thumbnails of AVIF pages inside archives and the tiles of a picture too large for the graphics card still ask Windows, so on a Windows without the codec those are missing. An HDR or wide-gamut AVIF is converted by its matrix alone, with no tone mapping, and its ICC profile is not applied. Not built yet (RV-110 stays open): JPEG XL and AVIF through FFmpeg when it is beside the program; a C decoder for JPEG XL, if one fit to read untrusted files exists, is to be put to the owner. A VP9 film in 4:4:4 RGB (profile 1), which Windows cannot decode, stays black and paused with no word.

## 2026-10-12: D-110 Everything of ours out of Explorer's menu; the picker as a details list; Hide; the speed button dragged; Settings at the menu's root

- Context: the owner, 2026-10-12, in two messages: Explorer's right-click menu shows several Rubraview entries where older versions were registered too, and wants one action that removes all of them; the status line at the bottom of the open-file screen is far too large; that screen should also be shown as Explorer's details view, chosen by the reader; a button that lets go of every picked file; Settings reachable from the menu at the top left, not only the toolbox; a Hide button on the title bar that puts the floating boxes away until the picture is clicked; and the speed button dragged up or down, or the wheel over it, changes the speed by 0.1x, a click going round as before.
- Decision:
  - **Wipe** (`rubraview_pal_shell_menu_wipe`, `--wipe-shell [--all-users]`, Settings > Explorer > "Remove everything, older versions' too"): first what Remove does (file types given back to whoever had them, the menu), then a sweep of both hives for what Remove never looks at — every verb under a `shell` key that is ours (`rubraview_shell_entry_is_ours`: its key name begins with "Rubraview", or its command starts a file named `rubraview*.exe`) on any `SystemFileAssociations` type, on `*`, `Directory`, `Directory\Background`, `Folder`, `Drive`, `AllFilesystemObjects` and the known extensions; `Rubraview.*` types, an extension still pointing at one going back to the type kept beside it (`Rubraview.Was`) as Remove gives one back; `Applications\rubraview*.exe`; `Software\Rubraview\Menu`; and every package of the menu's family (`GetPackagesByPackageFamily`), not only the one whose name was kept. The button does this user's hive in the viewer and every user's through the program run again as an administrator, in that order, because an administrator's process may be another account.
  - **The picker**: `files.picker_details` (Settings > Files, and the picker's own button "Details" / "Tiles") shows a row a file with a small picture, name, type, size and date under a line naming the columns. `picker_layout` puts the view and the window's size into the picker's geometry before it is drawn, pressed or moved by a key. "Clear" is now "Deselect all" (it was there; its name did not say so). The status line is 13 px (it was a third of the bar's height, 32 px) and the bar 76 px.
  - **Settings** is an item at the menu's root. It was the eighteenth item of File, and a level shows fifteen: Export, Batch, Settings and Quit could not be reached at all. File's five items about the file on screen went under "This file", and a test holds every level of the built-in menu to what the box shows.
  - **Hide** (`hide_boxes`; the title bar's first button, and Show > Hide all): the title bar and both boxes are not drawn and do not answer the pointer, the toolbox's own window is hidden; the next press of a mouse button in the window brings them back and is nothing else.
  - **Speed**: the speed tile is decided on release, as the subtitle and seek tiles are. Not moved, it goes round (1, 1.25, 1.5, 2, 0.5, 0.75; from a speed between, to 1x). Dragged, a tenth every twelve pixels, up for faster; the wheel over it a tenth a notch. Whole tenths from 0.3x to 4x.
- Alternatives: for the wipe, deleting only by key name — misses verbs an older build named otherwise; matching "rubraview" anywhere in a command — would take another program's verb that is merely given a rubraview file. For Hide, letting the click through to the picture — the reader would turn a page to get a menu back.
- Consequences: the wipe takes the file types too: after it Windows opens those types with what it had before, and Register puts back what is wanted. Windows' own per-user "Open with" memory (`Explorer\FileExts`, UserChoice) is not touched. Not done: sorting the details view by a click on a column (the order is Settings > Files > Sort by), a page's size inside an archive, a folder's size. The wheel over the speed tile does nothing in the detached toolbox, as with the zoom tile.

## 2026-10-12: D-111 The list window changes the list: rows checked, taken out, reordered, files dropped in, saved and loaded

- Context: the owner, 2026-10-12: in the list the toolbar's list button opens — an R button left of X that sizes the window when dragged; at the left of every row an X that takes it out of the list, up and down arrows, and a checkbox; the checked rows reordered by dragging; a Del button for the checked rows, Save and Load, files dragged in from outside landing at that place in the list, and an abs/rel button for how a saved list names its files.
- Decision: the playlist window (`P`) of a folder's files or a playlist is editable (`rubraview_listwin_t.editable`); a book's pages are not a list of files and stay as they were. Under the title a bar: **Del** (the checked rows leave the list; the files stay on the disk), **Save**, **Load**, **Rel / Abs**. Left of every row a checkbox, `x`, up and down. A row's name pressed is decided on release: not moved, the viewer goes to that file; carried, it moves — with every other checked row when it is checked itself — to the line shown between two rows. **R** in the title sizes the window from its top-left corner. Files let go over the window (`drop.has_point`, `x`, `y` from `DragQueryPoint` or the drop target's point) join at the row they are over; those already in the list, folders, and files the viewer does not open are left out. Every change builds the new sequence and opens it as a set in the order given (`list_apply` over `open_set_ordered`), the page being read kept on screen — the next one when it left — and a film going on from where it was. A list is saved as `.m3u8` or `.rvlist`, its files named from the list's own folder (`rubraview_path_relative`; `files.list_paths = relative`, the default) or by their whole paths (`absolute`); a file on another drive is whole either way.
- Alternatives: editing the pages in place without reopening the set — the page table, the cache and the read-ahead are all built by index, and "Add to the list" (D-100) already reopens; a new file format carrying the choice — both formats already take either kind of path when read.
- Consequences: a folder whose list is changed becomes a set of files ("Playlist" in the title), as with Add to the list: Settings > Files > Sort by no longer orders it. A list cannot be emptied (asked to take out every row, it takes out none and says so). A change reloads the picture on screen. Not done: several rows carried past the window's edge do not scroll it (the wheel does, while carrying); Del and the row's `x` cannot be undone with `Ctrl+Z`; a drop is at most sixteen files, as any drop is.

## 2026-10-12: D-112 RV-110's rest: J40 not built in; thumbnails and tiles by the built-in decoders; BigTIFF; a JPEG XL that moves; a film Windows cannot decode says so

- Context: the owner, 2026-10-12, twice "as recommended": first J40 assessed before anything is built, then — the assessment being against it — the item closed and the rest of RV-110 done. The assessment (the plan's "J40 assessed"): 28 of 61 files opened, two kinds shown wrongly without a word, no commit since 2022-12, three heap overflows reported in 2026 and unanswered; no other JPEG XL decoder in C is known.
- Decision: (1) J40 is not built in. JPEG XL stays with Windows' codec or the user's FFmpeg (D-108). (2) Every reader of a picture's bytes takes one road, `rubraview_wic_decoder_for_bytes` (Windows' codec, AVIF by ours first, JPEG XL by FFmpeg, a BigTIFF rewritten), with the caller's own factory: the thumbnails of an archive's pages and the tile thread had each called Windows' codec alone. A file the shell has no thumbnail for is read by ours when it is an AVIF or a JPEG XL of 64 MB or less. (3) The tile thread turns what ours reads into the uncompressed TIFF once per picture (`rubraview_wic_own_tiff`) and reads its bands from that, instead of decoding the whole picture for every band. (4) A BigTIFF under 4 GB is read by Windows' TIFF codec after its directories are written out again in the ordinary form behind the file's own bytes (`src/core/bigtiff.c`): nothing is decoded by us, the strips stay where they are, compressed as they are. (5) A JPEG XL that moves (the header's `have_animation` bit, read without decoding: `rubraview_image_is_animated`) is read whole by FFmpeg when it is opened, every picture already laid together, and played as any animation; 768 MB or 4096 pictures at most — past that the pictures that fit are played as the whole — and without FFmpeg it is its first picture as before. (6) A film whose picture Media Foundation names but never decodes (VP9 4:4:4) fails the open as a missing codec — FFmpeg is then tried, or the notice says so — where it used to play its sound over black.
- Alternatives: libtiff vendored for BigTIFF — a library and a ledger entry for what a hundred lines of directory rewriting give, since the strips are ones Windows already decodes; a second frame reader that seeks in a JPEG XL — FFmpeg gives a frame's time only by decoding it; sending a moving `.jxl` down the film path — the extension would have to be both a picture and a film.
- Consequences: a BigTIFF's sub-directories (EXIF, GPS, reduced pictures) are not carried into the rewritten directories, so its details show less than an ordinary TIFF's. A BigTIFF of 4 GB or more, or with a value past four bytes, is still refused. Thumbnails of AVIF pages decode the page at full size (AV1 has no reduced decode). A moving JPEG XL holds every picture in memory. The film check reads up to 200 samples before playback starts; only an error or the picture's end without a picture fails the open, a long run of sound first does not.

## 2026-10-12: D-113 FFmpeg DLLs of another version are said to be that, not "no FFmpeg"; RV-110 closed

- Context: measuring D-112 on the VM, a set of FFmpeg 8 DLLs beside the program (the build takes 9.0: `avcodec-63`) looked exactly like none: a `.jxl` said "no FFmpeg beside the program" and a film "FFmpeg DLLs beside rubraview would play it" — advice already followed. The owner, 2026-10-12: do both things proposed — say so, and close RV-110.
- Decision: `rubraview_pal_ffmpeg_refused` is true when FFmpeg did not load and an `avcodec*.dll` lies beside the program. The film's failure notice (`rubraview_media_failure_text_with`), the JPEG XL notice (`rubraview_jxl_failure_text`) and `--probe`'s first line then say the DLLs beside the program are not 9.0 (`avcodec-63`) and were not loaded. RV-110 is closed and its plan archived.
- Alternatives: carrying the reason out of the loader (which DLL, which version it reported) — more to say than a reader can use; "another version" with the one wanted is what tells them what to fetch.
- Consequences: a set of the right version that fails for another reason (a DLL missing from the five, a build without a function the program binds) is also called "not 9.0"; that has not been met. The version is written into the notices and has to follow the vendored headers when they change.

## 2026-10-13: D-114 FFmpeg 9.0 DLLs that would not load are not "another version"; what was left unmeasured of RV-110, measured

- Context: the owner, 2026-10-13: go on with what was left — the "not run" of T164 and T165. D-113 had named its own gap: a set of the right version with a DLL missing was also called "not 9.0 (avcodec-63)", with `avcodec-63.dll` lying there.
- Decision: `rubraview_pal_ffmpeg_refused` gives a kind (`rubraview_ffmpeg_refusal_t`): another version (an `avcodec*.dll` that is not this build's), or not whole (this build's `avcodec-63.dll` is there and the set did not load). The film notice, the JPEG XL notice and `--probe-media` say for the second that the FFmpeg 9.0 DLLs could not be loaded and all five are needed; `--probe-media` names the four fellows. Its "cannot open" line takes the same text as the notice on screen (it kept "FFmpeg DLLs beside rubraview would play it"). (D-113 wrote `--probe`; the option is `--probe-media`.)
- Alternatives: asking the loader which DLL failed — it tries five names and binds some hundred functions; "all five are needed" covers the case a reader can mend, and the diagnostic lists them.
- Consequences: a whole 9.0 set that fails for another reason (a 32-bit build, a function missing) is also told "could not be loaded (all five are needed)"; not met. Measured on the VM and recorded in T164 / T165: tiles of an AVIF and a JPEG XL are the read ones; a moving JPEG XL past 768 MB plays what fits (97 of 100 pictures) and says nothing of the rest; a film with 30 s of sound before its first picture opens without the "cannot decode" notice, and its position in the title is the shown picture's, as in 0.0.60 (RV-111). Left: an AVIF on a Windows without the HEIF and AV1 extensions (not taken off the shared VM; the path was run by the JPEG XL, for which the VM has no codec), a BigTIFF near 4 GB.

## 2026-10-13: D-115 A film is where its sound is when no picture belongs to now; a late first picture is read from the start, not rewound to (RV-111)

- Context: the owner, 2026-10-13: look at the backlog and go on. RV-111 stood first: on the VM a film with 30 s of sound before its first picture read 00:30.000 in the title throughout (MKV) or stopped at its last picture's time while the sound went on (MP4). Looking for why, a trace of Media Foundation's reads showed worse for the MKV, and since 0.0.61: `first_picture_arrives` (D-112, on the software path) read 200 samples of sound, found no picture and set the reader back to 0 — which for an MKV lands on its first picture. The sound then began at 29.93 s, the heard position counted from 0, the pictures waited for 30 s on a clock that ended at 6.1, and the film stood still for good. 0.0.60, by the code (no such check on the software path), read that file's sound from the start; it was not listened to.
- Decision:
  - `first_picture_arrives` rewinds only when the first picture came within a second of the start. A later one, or none within 200 samples that is not a refusal, has the reader released and opened again (card path and software path), so the file is read from its beginning by a reader that has read nothing.
  - The heard position starts where the first sound after opening or a seek really is: when that is more than 0.05 s from where it was asked for, `deliver_audio` restarts the output's position there (`rubraview_pal_audio_flush`, the ring being empty then).
  - `rubraview_media_position_running`: a running film's position is its picture's time while the clock is within half a second of that picture, and the clock (kept inside the file's length) otherwise. `media_tick` applies it after the frame loop; before the first picture since opening it is the clock when the sound is the master (`media_sought` keeps the place asked for after a seek until its picture lands).
  - A moving JPEG XL cut at the limits (768 MB, 4096 pictures) says "showing the first N pictures — the rest does not fit in memory" (`rubraview_pal_anim_cut`).
- Alternatives: always opening the reader twice instead of rewinding — a second parse of every film, slow over a network (RV-102's concern); showing nothing until the first picture is due — the picture ahead of its time does no harm and the position now tells the truth; counting the cut animation's pictures — it would mean reading them all.
- Consequences: subtitles, lyrics, the A-B repeat and the cue sheet follow the clock through a stretch without pictures. With the wall clock as master (no sound device) the first picture still moves the clock to its own time, so such a film starts at its first picture there: left as it was, not measured. FFmpeg's reader needed nothing but the position rule. A first picture between 1 s in and 200 samples of sound costs a second opening. Not exercised on the VM: the card path's second opening (no decoder on its card) and the restart of the heard position (after the second opening the sound began at 0).

## 2026-10-13: D-116 Where the Windows 11 menu package is registered, the registry's verb is not kept beside it (RV-104)

- Context: the owner, 2026-10-13: go on with the next. RV-104 was D-100's Explorer behaviour, never seen with a real pointer. Seen on the VM (Windows 11, build 26200, the MSI of 0.0.64 installed): under **Show more options** a picture's menu had "Rubraview" twice. With the registry's verb switched off (`LegacyDisable`) one was left, so the other is the package's: Windows 11 lists a package's items in the classic menu as well as in its own. D-86 wrote both for every user, taking the classic menu to be the registry's alone.
- Decision: when `rubraview_pal_shell_menu_register` has registered the package (the `BOTH` result) on Windows 11 (build 22000 or later, `RtlGetVersion`), it takes the classic verbs off every extension again, in HKLM and in this user's HKCU: the package carries both menus. Without the two files, or when Windows refuses the package, the registry's verbs stay as before. On Windows 10 both stay.
- Alternatives: keeping the registry's verb and hiding the package's item in the classic menu — an `IExplorerCommand` is not told which menu asks; leaving it — every MSI install shows the entry twice.
- Consequences: under **Show more options** the items now start the program once with the whole selection, as Windows 11's own menu does, where the registry's verbs started it once a file ("a window each" for "Open in a new window"). Another user's own HKCU registration, made before an administrator registered for every user, still shows twice for that user until they remove or re-register it; so does "Register for this user" made after it. That the package shows in the classic menu was seen on build 26200 only. Windows 10 with a registered package was not looked at.

## 2026-10-13: D-117 The information window says what the file holds, not what the codec handed over (RV-103)

- Context: the owner, 2026-10-13: go on with the next. RV-103 was what D-93..D-99 built and nobody had seen. On the VM (0.0.65, FFmpeg 9.0 DLLs beside it) four lines of the information window were wrong: a GIF's pixels read "8-bit grey"; a WebP with no profile in it read "Colour profile: embedded"; a Matroska film opened by FFmpeg read "Picture codec: 0x00000000"; and FFmpeg's streams had no "Decoded as" line, which D-95 promises for each.
- Decision:
  - A palette format (WIC's 1, 2, 4 and 8-bit indexed) is "indexed colour" whatever its channel count: WIC counts one channel for it, as for grey.
  - "Embedded" is said only when one of the frame's colour contexts is a profile with bytes in it (`frame_has_profile`). A context that only names a colour space, which Windows' WebP codec gives every file, is not a profile.
  - A stream whose container gives no four-letter code, or one that is not letters, is given one made of the codec's name (`rubraview_info_fourcc_or_name`: "h264" is H264, "vp9" VP9); a code that ends in blanks is written without them.
  - FFmpeg's played streams say "Decoded as": the frame's size as 32-bit BGRA, the header's rate and channels as 32-bit float, the words Media Foundation's block uses.
- Alternatives: the WebP profile read from the VP8X flag in the core — it is, in the WebP block below; the Picture block is the same for every format and asks the codec; the codec's long name in place of a code — the stream's block has it, this line is the short one.
- Consequences: any other picture for which Windows names a colour space without a profile reads "none" too (a JPEG with only EXIF's sRGB mark may be one: not looked at, in either build). As with Media Foundation, FFmpeg's "Decoded as" for sound is the header's format and does not follow a change found while decoding. Seen right on the VM and left as they are: a WebP decoded by Windows reads "32-bit colour with transparency" and 72 dpi whether or not it has an alpha channel (Windows' codec gives every WebP so); FFmpeg calls H.264 "lossy or lossless" (its own description of the codec).

## 2026-10-13: D-118 On FFmpeg a film's time starts where the file's does, and the sound is fed while pictures wait (RV-112)

- Context: the owner, 2026-10-13: make or fetch an HE-AAC MKV and see the sound fix through. Two public samples (HE-AAC with its core rate in the header, HE-AACv2) played right on both decoders. To meet a format that changes while decoding, a transport stream was made whose sound goes from 22050 Hz mono to 48000 Hz stereo after ten seconds. Windows' decoder played it; with FFmpeg chosen it stood still after a third of a second, and so did its first ten seconds alone, in 0.0.63 as in 0.0.66.
- Two causes, both in the FFmpeg backend and neither about the format:
  - A transport stream's time does not start at 0 (1.4 s as FFmpeg writes one, hours in a recording). The pictures kept the file's stamps while the sound device's clock began at 0, so the first picture waited for a time the clock reached only if the sound kept coming.
  - A transport stream holds a third of a second of pictures, then the sound for them. The decode thread decoded each packet as read: four picture slots filled, it waited on them (300 ms a picture, then dropped it), and the sound it had not yet read ran out — and with it the clock the pictures waited for.
- Decision:
  - A stream's stamp less the file's start (`AVFormatContext.start_time`) is the film's time (`stream_time`), for pictures, sound and the text subtitles read from the file; a seek adds it back.
  - Picture packets that cannot be decoded yet (no free slot) wait in a queue of 64 while reading goes on, as long as the sound waiting for the device is under a quarter of a second; with enough sound waiting, or while paused, nothing is read ahead. A full queue decodes as before (wait, then drop).
- Alternatives: more picture slots — each is a whole frame of memory, and a stream cut in larger blocks would need more again; a thread for the sound — a new thread for what an order of reading solves.
- Consequences: any file whose time starts elsewhere (TS, M2TS, a cut MKV) is shown from 0 on FFmpeg, as Media Foundation shows it. Up to 64 compressed pictures are held in memory. Not changed: when a file's sound begins later than its pictures, the device's clock still starts with the first sound (Media Foundation's backend restarts the heard position there since D-115; FFmpeg's does not). The sound fix of D-95 itself needed no change: see T170.

## 2026-10-13: D-119 A film's sound device is exchanged under a lock, and a switched track's device is as the old one was left (RV-112)

- Status: Accepted (owner, 2026-10-13: the plan `2026-10-13-RV-112-mf-audio-swap` approved, "계획대로 진행").
- Context: both media backends close the sound device and open another on the decode thread — Media Foundation's when the sound's format changes while decoding (D-95) or a track of another format is chosen, FFmpeg's on such a track switch — while the caller's thread asks the same device for its position, starts and stops it, sets its level and asks whether it has drained. Between the closing and the new pointer the caller could call into a device that was gone. Never seen to fail.
- Decision:
  - Each film has one `SRWLOCK` (`audio_lock`). The decode thread holds it exclusively for the whole exchange (close, free the ring, open, restore level and play state); the caller's thread holds it shared around every use of `audio`. The wish (playing, level) is written inside the shared hold, so a device opened meanwhile reads it afterwards. The device's own thread takes neither: no circle.
  - Reading the code for it showed a fault of FFmpeg's track switch: the device opened for a track of another rate or channel count was left as it is opened, silent and at full level, so that track was not heard and, the sound being the clock, the film stood. It is now set playing unless paused and given the level last asked (`audio_gain_millionths`, as Media Foundation's backend already kept).
- Alternatives: the exchange prepared outside the lock and only the pointers swapped inside — two devices open at once, and the ring is part of the film; an atomic pointer with the old device closed later — the caller's thread would still be inside it.
- Consequences: a call from the caller's thread waits while a device is being opened (once a change). The decode thread's own use between exchanges takes no lock (it is the only one that exchanges).

## 2026-10-13: D-120 The backlog's rest: every picture of a Matroska film, the first sound at its time, the format now played, three more detail blocks, stale temporary files

- Context: the owner, 2026-10-13: "백로그 다 진행". What RV-112 listed as known and left, and the two observations under Later.
- Found on the way, the largest of them: with Windows' decoder a Matroska film showed **every second picture only** (a film of 24 a second: 12). Windows' Matroska source names half the real rate in the stream's type; the reader's video processor, which D-107 switched on for software decoding in 0.0.56 (and the card's path always had), makes the pictures come at the named rate. 0.0.50 gave all 24 with the same source. MP4 and transport streams name their rate rightly and were whole.
- Decision:
  - The video processor is told to leave the rate alone (`MF_XVP_DISABLE_FRC` on the processor's attributes, reached through `IMFSourceReaderEx::GetTransformForStream`); the pictures keep the times the file gives them.
  - The rate shown, and the frame step that goes by it, is measured from the pictures' own times (`rubraview_rate_meter_add`: sixteen steps), and replaces the named one when they are more than a twentieth apart (`rubraview_media_rate_differs`); `picture_rate` in the backend's table, asked each tick.
  - FFmpeg's reader places the first sound after opening, a seek or a new device at that sound's own time (`audio_base`, `audio_begun`, as Media Foundation's does since D-115): a sound track that begins half a second after the pictures was heard half a second early.
  - `audio_format` in the backend's table gives the rate and channels played now; the information window's Sound line and the analyser go by it, each tick.
  - The information window has a block for TIFF and the camera files built on it (kind, directories, compression, predictor, colour, samples, layout, mosaic, camera, DNG version, what it carries), for AVIF / HEIF / Canon CR3 (brands, items, sequence, largest picture, AV1 or HEVC with profile, level, depth and chroma, transparency, colour, turned or mirrored) and for JPEG XL (bare or in boxes, size, depth, extra channels, animation, level, whether it holds a JPEG that can be had back). A picture read by our own AVIF or JPEG XL reader is named by its own format in the Picture block, not by the TIFF it is handed to Windows as.
  - Before a state file is written, the temporary files beside it of processes that no longer run are removed (`sweep_state_temps`, `rubraview_path_state_temp`); one whose process lives, this process's own, and any other `.tmp` are left.
- Alternatives: plain video processing first and the advanced one only for 10-bit films — the card's path needs the advanced one and would still lose pictures; doubling the named rate for Matroska — a guess; the stale files removed at start — they are found where they are written, and a write is when the folder is known.
- Consequences: the stream block still shows the rate the source names ("12.000 a second"); the Film block above it shows the measured one. Not seen: the card's path (the VM's card decodes nothing) — the same call is made there. A raw file's block describes its first directory, which is often its small preview. The first-key observation (a key right after a start ignored) did not show in ten starts with another window in front, and is closed.

## 2026-10-13: D-121 A review against D-32: numbers read by hand, sizes multiplied by hand, and comparisons written out go through proven or the one shared set

- Context: the owner, 2026-10-13: with the backlog empty, "코드 리뷰 및 proven 에 맞게 잘 짜여졌는지 확인". D-32 is the measure: proven for what it already solves (checked size arithmetic, number parsing, sorting), one set of string comparisons in `core.h`; `u8str_t`, the PCM ring, `malloc` on decoder threads and `snprintf` into fixed buffers stay.
- Found:
  - Nine places built a decimal number digit by digit with `value * 10 + digit` in a signed type: LRC time stamps and cue sheets (`lyrics.c`), VobSub time stamps and sizes (`vobsub.c`), the reading history (`history.c`), ComicInfo.xml (`comicinfo.c`). A long enough run of digits in such a file overflowed, which for a signed type is undefined. None went through `number.h`.
  - Thirty-five heap allocations multiplied a width, a height and a pixel size (or a count and an element size) inline, unchecked; the arena's side had `rubraview_arena_alloc_array` since D-32, the heap's had nothing.
  - `qsort` twice in `arcedit.c` where D-32 chose `proven_array_sort`; one private copy of the view comparison in `main.c`; twenty-three comparisons written as `len == N && memcmp(ptr, "literal", N) == 0`, each right, each with its length twice; `strcpy` eight times into one fixed buffer in `pal_gpu_resample_win32.c`.
- Decision:
  - `rubraview_digit_append` / `rubraview_digit_append_i32` (`number.h`): one more digit through `PROVEN_CKD_MUL` and `PROVEN_CKD_ADD`, staying at the type's largest value instead of overflowing. The nine places and D-120's own (`rubraview_path_state_temp`) use it.
  - `rubraview_malloc_product(a, b, c)` (`core.h`): the product through `PROVEN_CKD_MUL`, NULL when it does not fit. Every allocation whose size is a product uses it.
  - `proven_array_sort` in `arcedit.c`; `rubraview_u8_eq` and `rubraview_u8_eq_lit` for the comparisons; `snprintf` with the buffer's size for the eight copies.
- Kept, and why: seven arrays grown by hand with `realloc` (APNG frames, a job's volumes, a shell request's text, a folder listing, a moving JPEG XL's frames and one PNG buffer) — each lives on the heap across threads or past the frame, grows by doubling a count that memory bounds long before a size overflows, and a `proven_array` would want an allocator handed through code that has none; `strcmp` on C strings from the archive library; `memcpy`.
- Consequences: no behaviour changes for a well-formed file. A file with an absurd number in it reads as the type's largest value where it read as whatever the overflow left. Nothing was seen to fail before: the old loops were not run against such input.

## D-122: The Microsoft Store's Package, Built By Rubrapack (2026-10-14)

- Context: the owner registers as a publisher in the Microsoft Store and wants the viewer there; the package is to be built with rubrapack. Partner Center gives the product three values (identity name, publisher, publisher display name) which a package must carry exactly.
- Decision:
  - `resources/distribution/rubraview-store.toml`: one more rubrapack source, for an MSIX of the same folder the zip and the MSI are made of — the program, the manual, the notices and licences; a Start menu entry; the viewer's 26 file types as four kinds (picture, comic book, video, music). `scripts/package-store.sh` (`make store`, after `make package`) builds `dist/rubraview-v<version>-store.msix` and lints it.
  - The three values are not kept in the repository: the script reads them from the file `RUBRAVIEW_STORE_IDENTITY` names and passes them to rubrapack as definitions. They are not printed.
  - The Store refuses a version whose first part is 0 and keeps the fourth: the package's version is the program's with the first part one higher (0.0.70 is 1.0.70.0). The program's own version is not changed by this.
  - The package is not signed: the Store signs what it accepts. It is not uploaded to the GitHub release.
  - The file types are in the package's manifest because a packaged program's own registration (`--register-shell`) is written to the package's private registry, which Explorer does not read. `scripts/check-store.py` (in `project-check.sh`) fails when the source's list and `rubraview_shell_extensions()` differ.
- Not in this package (RV-113): Windows 11's own right-click menu (the menu DLL reads its items, with the program's full path, from a registry key the registration writes; a package needs them some other way, and a class id of its own beside the installed viewer's); the program does not know it is packaged, so Settings > Explorer still offers a registration that has no effect there; FFmpeg's DLLs.
- Consequences: three ways to get the viewer (zip or exe, MSI, Store). Whether Partner Center accepts the package is known only after an upload.

## D-123: The Slide Show's Stop And Delay In The Toolbox (2026-10-14)

- Context: the owner asked for a stop button while a slide show runs, and for the delay between slides to be set where a film's speed is, with the wheel over the button. The slide show's own tile was there ("Stop slides") but drew the slide-show sign, which does not say "stop"; the delay had keys only (`[`, `]`).
- Decision:
  - While the slide show runs its tile draws the stop sign (`rubraview_action_icon`).
  - A second tile in the `slideshow` profile, `interval_cycle`, whose face is the delay ("3 s", "2.5 s"). The wheel over it, or a drag up or down, moves the delay half a second a step, from half a second up; a tap goes round 1, 2, 3, 5 and 10 seconds (a delay that is none of them goes to 3 first). It is handled as the speed tile is (D-110): the tap is decided on release.
  - The profile's speed tile (`media_speed_cycle`) is left out while the slide show runs: the delay stands for it. A film's speed keys still work.
  - Every change of the delay, by key too, is said on screen ("slide delay 3.5 s").
- Consequences: the delay is still the session's (3 s at start), not a setting.

## D-124: The Toolbox Strip In Groups, The Name On Several Rows (2026-10-14)

- Context: the owner: the strip is to show its parts in order — the bar, the file's name on one row or several, then the icons a group a row, wrapped onto more rows where the strip is narrow. It was one run of up to forty buttons, eight a row, and a long name was cut.
- Decision:
  - A `group` line in the boxes document starts a new group in a toolbox profile; a tile carries its group. The built-in profiles are grouped: moving between files; playing; sound; size (zoom and fit); turning, layout and the slide show; files; the window. The slide show's two tiles are a group in front.
  - `rubraview_toolbox_layout_shaped`: each group starts a row and goes on to more rows when it is longer than the strip's columns (the grip's setting), with half a gutter more between two groups. The old `rubraview_toolbox_layout` is the same with one group.
  - The name takes one to three rows, measured as it is drawn (`strip_title_plan`, kept until the name, the width or the place's length changes). The place or the time stays at the right of the name's row when both fit; otherwise it has the last row to itself.
- Consequences: the strip is taller (a film's: six rows of buttons where it had four) and its buttons moved; within a group the order is the old one, but for the file group, which starts with Open and Info. A tile whose caption shows while pointed at keeps the name's rows, so the box does not jump.
- Not run: the detached toolbox window with a name of several rows (it uses the same layout and follows its size).
- 0.0.72: the place's row is one more than the name's three, never one of them — in 0.0.71 a name of three rows and a place shared the third row. A name longer than three rows is cut to the longest beginning that fits, with an ellipsis (the renderer has no clipping; the cut is found by measuring).
- 0.0.73 (owner, 2026-10-14): a film's and a song's first group is Play / Pause, Stop, five seconds back and forward, the file before and the file after, in that order; A-B, the speed and the repeat are the second group. The other profiles keep moving between files first.

## D-125: The Subtitle Window (2026-10-14)

- Context: the owner: a window that lists the subtitles one a row and goes to the one clicked, outside the viewer's window; several chosen and moved earlier or later by 0.1 s or by an amount typed; each edited; an embedded subtitle taken out as a file or one put into the film. Then: a check box before each row and one above for all or none; the buttons and the field for the seconds above the list. Planned as RV-115 in four stages; this is the first.
- Decision:
  - `toggle_subtitle_window` (`Shift+C`, a tile in the film's toolbox, Subtitles > List window): a window of its own owned by the viewer, as the file list window is. Rows are `ui_listwin`'s; what they hold and what a click does is `ui_subwin` (tested without a window).
  - A plain click on a row goes to the subtitle and leaves the boxes alone; the box, or `Ctrl` + click, switches one; `Shift` + click chooses a run; the box in the header, all or none. The row of the moment playing is the last subtitle begun, so the mark does not vanish between two.
  - Earlier / Later move the ticked cues by the seconds in the field (digits and one point; 0.1 as it comes), start and end together, not before the film's beginning; the track is kept in order of start, so a cue moved past its neighbour changes place and its tick goes with it. This is apart from `Z` / `X`, which move the whole track's offset.
  - The moved times are in memory only in this stage; the header says so. The owner's answers for the rest: saving patches the file in its own format with the styling kept and takes the subtitle's name, the file before renamed with a number beside it; a subtitle put into a film makes a new file of the film's own format; Matroska read and written by our own code.
- Consequences: a third list window in `main.c`. Picture subtitles (VobSub, PGS) are listed by time and can only be gone to. With a SAMI of several languages only the language on screen is listed and moved.
- Not run: touch; a list longer than the window scrolled by the wheel and left to follow again (the wheel in the VM test fell outside the window); picture subtitles in the list; a SAMI of two languages.

## D-126: A Subtitle Saved In Its Own Format, Only What Changed (2026-10-14)

- Context: RV-115 stage 2. The owner asked whether a subtitle could be saved in its own format with the styling kept and only the needed parts rewritten, and decided: the new file takes the subtitle's name, the one before is renamed and kept in the same folder, numbered so that no name is taken twice.
- Decision:
  - Each reader records where a cue stands in the file's text (`rubraview_subtitle_place_t`: the start time, the end time, the text with its markup): SubRip / WebVTT the two clocks of the arrow line and the lines under it; SubStation the Start, End and Text fields of a `Dialogue:` line; SAMI the number of its `<SYNC Start=>`, the number of the empty SYNC that closes it (none when the next caption closes it), and the words after its `<P ...>`. The track keeps the text read (`source`), its path and whether it was read in the machine's code page.
  - `rubraview_subtitle_patched` copies the source and replaces only the places of the cues marked `time_changed` or `text_changed`, times in the format's own writing; the same place asked for twice is written once, places that overlap refuse the save. With nothing changed the result is the source.
  - A text is edited as the file holds it, on one line, a line break shown and typed as `\N` in every format (`rubraview_subtitle_cue_typed` / `_retype`); written back it is the file's own line end (SubRip, WebVTT), `\N` (SubStation) or `<br>` (SAMI). The cue shows the result at once, read as the reader reads it. An edit that leaves nothing to show is refused.
  - Saving (`subs_save`): the file on disk must still be what was read (for a file not in UTF-8: the source encoded back must equal it), and the new text must be writable in that encoding; else nothing is written and it says why. Then the file is renamed `name.oldN.ext` (the first N free) and the new one written under its name; if writing fails the old one is put back. The track is read again from what was saved.
  - `name.oldN.ext` is not one of the film's subtitles (`rubraview_subtitle_discover`).
  - In the window: a second click on a row, `F2` or `Enter` edits; `Ctrl+S` or the Save button saves; a changed row is marked. `Enter` is no longer passed to the viewer from this window (there it turns to the next file, and the changes were gone — seen in the VM).
- Consequences: the subtitle cue and track structs are larger. A cue whose text is longer than the reader's 4096 bytes has no text place and cannot be edited. Unsaved changes are lost with the film or the track, said on screen but not asked.
- Not done (of the plan's stage 2): adding and removing a cue; editing a time as a number; writing the Z / X offset into the file; a SAMI cue moved past its neighbour is not moved in the file (it is read in order here; another player may want the file in order); the offer to save as UTF-8 when the encoding cannot hold a character.
- Not run: Korean typed through the IME into a row (keys were ASCII in the VM); WebVTT on the VM (unit test only); a file not readable back in its code page.

## D-127: Matroska Read By Our Own Code; A Film's Subtitles Taken Out (2026-10-14)

- Context: RV-115 stage 3. The owner: Matroska is read and written by our own code, and MP4 too (MIT, from the specifications); what is written is tried in other players (mpv or SMPlayer on the VM). And for the stage after: a film is changed only when approved, through a temporary copy compared with the original and checked, the original renamed and kept in its folder.
- Decision:
  - `matroska.c` / `matroska.h`, written from RFC 8794 (EBML) and RFC 9559 (Matroska) alone — no code of FFmpeg, libmatroska or GPAC was read. It takes bytes and a size (a mapping; a film is never copied into memory) and reads headers only. `rubraview_mkv_open`: the EBML head and DocType (`matroska`, `webm`), the first Segment, `TimestampScale`, the tracks (number, type, CodecID, CodecPrivate, Language / LanguageBCP47, Name, whether it has ContentEncodings). `rubraview_mkv_walk_next`: every block in the file's order — SimpleBlock, and BlockGroup with its BlockDuration — its track, time and bytes. Sizes written as unknown (a live recording's segment and clusters) are followed; an open cluster ends where a top-level element begins.
  - Every size in the file is checked against what is left (`PROVEN_CKD_ADD` / `_MUL`); a size past the end has what is left; a file cut short gives what was whole and then stops. The test cuts a file at every byte under ASan.
  - `S_TEXT/UTF8` is written as SubRip (numbered, a block without a length three seconds); `S_TEXT/ASS` / `SSA` as SubStation: the track's own header, then `Dialogue: Layer,Start,End,...` from each block's `ReadOrder,Layer,...`. Picture codecs, WebVTT and tracks with ContentEncodings are not taken out, and it says which.
  - `extract_subtitles` (the Take out button of the subtitle window, Subtitles > Take out of film): the film is mapped and walked eight milliseconds a frame with its share shown; the same action stops it; the files are written only when the walk is whole. Names: `film.<language>.srt|ass`, no language part for `und`, `.2` for a second of a language. A file of that name with the same bytes is left; another is renamed `.oldN` first (D-126). Then the film's subtitle list is made again and the first new file shown, unless a subtitle with unsaved changes is on.
  - mpv 0.41.0 (the project's own Windows build) is kept on the VM for trying files in another player.
- Consequences: a film's embedded text subtitles can be had without FFmpeg, as files. Playing an embedded track directly still goes through FFmpeg; that is not changed here. The walk reads the whole film once (there is no index of subtitle blocks), at the disk's speed.
- Not done: MP4; laced subtitle blocks and EBML-void-padded oddities beyond what the test builds; a second Segment in one file.
- Not run: a film of several gigabytes (the slices and Stop were seen only on a 4.5 MB film, which ends within a frame or two); a file made by mkvmerge (ffmpeg's muxer made the VM's film; unknown sizes were ffmpeg's `-live 1`, read on Linux only).

## D-128: A Subtitle Put Into A Matroska Film, Checked Before The Film Is Replaced (2026-10-14)

- Context: RV-115 stage 4. The owner: changing the film is dangerous — ask and get approval first; write a temporary copy, compare it with the original and make sure it is sound, then rename the original, give the temporary file the original's name, and keep the original where it was.
- Decision:
  - `rubraview_mkv_put_plan` lays the new file out as pieces, each a range of the film's bytes or of bytes made here: the EBML head, a new SeekHead, the film's Info, Chapters, Attachments and Tags as they were, its Tracks with one more TrackEntry (number after the last, a TrackUID, `S_TEXT/UTF8` or `S_TEXT/ASS` with the file's header as CodecPrivate, the language the file's name gives, the file's name as Name, not default), every cluster in its order, and new Cues at the end. The old SeekHead, Cues and Voids are left out. The size is known before a byte is written.
  - Each subtitle goes as a BlockGroup (Block and BlockDuration) inside the cluster its time falls in, before the first block not earlier than it; that cluster gets a new header and loses its CRC-32, its blocks are the film's bytes. A subtitle no cluster's 16-bit clock can carry, or one before the first cluster, gets a cluster of its own. The first layout put each subtitle in a cluster of its own between the film's: ffmpeg read it, but mpv landed two seconds late after a paused seek and showed no subtitle — inside the cluster, as a muxer puts it, both are right.
  - The Cues keep the film's points with their cluster positions moved and block positions moved where a cluster was rebuilt, and gain one point per subtitle with its block's place and duration (a player seeking into a subtitle finds it).
  - `rubraview_mkv_check_*` compares the new file with the film: its tracks are the film's and one more; every block of the film's tracks is there in the same order with the same time, length and bytes; the new track's blocks are the subtitles put in; the file is read to its end; every cue point leads to a cluster; the size is the planned one. The unit test changes one byte of a cluster and cuts one byte off: both are refused.
  - In the viewer: Put in film (the subtitle window) — the subtitle must be a SubRip or SubStation file with no unsaved change. The first press says what will happen and names the kept file; a second within six seconds goes on (Delete's idiom). The film is mapped; the new file is written as `film.mkv.rubraview-part` (not a film's extension, so no list shows it half written) with the platform's new piece-by-piece writer (`rubraview_pal_fs_writer_*`, `CREATE_NEW`: it never writes over a file), a slice a frame; then mapped and checked a slice a frame; then the film becomes `film.old1.mkv` (the first free number), the new file takes its name, and the folder is opened again on it. If the film cannot be renamed while playing it is closed and tried again; if the new file cannot take the name, the film is put back. Any failure, a stop, or closing the viewer removes the temporary file and leaves the film as it was. It needs free space for the new file and 64 MB.
  - WebM is refused (it holds only WebVTT subtitles); so are SAMI and WebVTT files for now.
- Consequences: the folder holds two films afterwards, and lists, players and the viewer's own page list show `film.old1.mkv` too — as the owner asked. Playing the embedded track in this viewer still needs FFmpeg (D-127); Take out or another player shows it without.
- Not run: a film of gigabytes (the slices, Stop and the progress on the button), a film made by mkvmerge, a film whose clusters carry CRC-32 (ffmpeg's default output had none here), a rename refused because the film is playing, a full disk.

## D-129: An MP4 Film's Subtitles Taken Out, By Our Own Code (2026-10-14)

- Context: RV-115, the MP4 half of taking out (the owner: MP4 by our own code as well, MIT, from the specifications).
- Decision:
  - `mp4.c` / `mp4.h`, written from ISO/IEC 14496-12 (boxes) and 3GPP TS 26.245 (timed text) alone. Boxes of 32-bit size, a 64-bit one (size 1) and "to the end" (size 0); only `moov/trak/mdia/minf/stbl` is descended. A track: `tkhd`'s id, `mdhd`'s timescale and language (both versions), `hdlr`'s handler, the first sample entry's format, and the tables `stts`, `stsc`, `stsz`, `stco` / `co64`, each count checked against its box. A `moof` or an `mvex` refuses the file (fragmented).
  - The samples of a track are walked through its chunks (`stsc` for how many a chunk holds, `stco` / `co64` for where, `stsz` for how long, `stts` for when). A `tx3g` sample is a 16-bit length, UTF-8 text and boxes of style: the text is written as a SubRip cue, its line ends as one; a sample of no text is a gap between subtitles and is passed over; UTF-16 text is not written. WebVTT, TTML, CEA-608 and QuickTime text are found and not taken out, and it says so.
  - Take out tries Matroska first, then MP4. An MP4's subtitle samples are few and their places known, so they are read at once rather than walked a slice a frame; the files are named and written as for Matroska (`film.kor.srt`), the same rule for a name already taken.
  - D-128's swap now opens the folder again on the film by its path, not its old place in the list (in date order the new film is the newest and the old one keeps its date). The Put in and Take out buttons answer whatever is on screen, so a press that cannot do anything says why (one of three VM runs of 0.0.77 started nothing, with no word: the film was likely not up yet).
- Consequences: the extracted SubRip equals FFmpeg's extraction from the same file. An edit list (`elst`) is not applied: FFmpeg's output starts its subtitle at zero and needs none; a file whose subtitle track is shifted by one would come out shifted.
- Not run: an MP4 made by another muxer than FFmpeg, one with an edit list on the subtitle, a QuickTime `.mov`.

## D-130: A Subtitle Put Into An MP4 Film, Nothing Of It Moved (2026-10-14)

- Context: RV-115, the last part: the owner's "the new film keeps its format" (an MP4 stays an MP4) and "MP4 by our own code"; D-128's rule (asked first, a checked temporary copy, the original renamed and kept) holds.
- Decision:
  - Nothing of the film moves, so no sample offset of any track has to be written again: the film's `moov` becomes a `free` box of the same size where it stands (its four letters are the only bytes that change); after the film's end come an `mdat` with the subtitle's samples and a new `moov` — the old one's boxes byte for byte (`mvhd`'s next track id raised past the new track), then a 3GPP text track as FFmpeg writes one: `tkhd` enabled, an edit list of the whole track, `mdhd` (1000 ticks a second, the language from the file name), `hdlr sbtl` named after the file, `nmhd`, a self-contained `dref`, a `tx3g` sample entry (centred at the bottom, white, Arial 16, its `ftab`), `stts` / `stsc` / `stsz` / `stco` (or `co64` past 4 GB) for one chunk.
  - The samples are the subtitle's words (any format the viewer reads, a SAMI's language on screen), a 16-bit length and UTF-8; a gap between two subtitles is an empty sample, so the track's time runs on; a subtitle that overlaps the next ends where the next begins. Markup does not go in (tx3g styles are boxes, not tags).
  - A film whose last box runs "to the end of the file" (size 0), a fragmented one, or one that does not end where its last box does, is refused.
  - `rubraview_mp4_check_*`: every byte of the film equal in the new file but the four letters `free` (compared a slice a frame); the new file read as an MP4 has the film's tracks with the same tables and one more; the new track's samples are where, when, how long and what the plan said; the size is the plan's.
  - In the viewer, Put in film tries Matroska and then MP4; the rest (two presses, the temporary file, the check, the swap) is D-128's.
- Consequences: the new MP4 is larger than the film by the old `moov` it keeps as `free` plus the new one (here 4.58 MB → 4.71 MB), and its head is at its end, so it does not start playing before it is all downloaded — the film's format is kept, its layout for streaming is not. Typing styles in SubRip (`<i>`) are lost in an MP4.
- Not run: an MP4 over 4 GB (`co64`), one with its `moov` at the start (moved there for streaming), one from another muxer, QuickTime `.mov`.

## D-131: The Subtitle Window's Leftovers — Changes Not Lost Unasked, Z / X Saved (2026-10-14)

- Context: RV-115's stage-2 leftovers. Changes not saved were dropped with a word on screen when the reader turned to another film or subtitle; the Z / X offset was kept for the film but never reached the file; a Matroska film with a SAMI or WebVTT on armed Put in film at the first press and refused only at the second.
- Decision:
  - `subs_may_leave`: with a cue moved or edited and not saved, whatever would drop it — `go_to_spread` to another page (so the keys, the tiles, the page bar and the next film after one ends), `open_path`, a set opened, a sibling archive, another subtitle (the list, the next-track key, CC, the subtitle box's X), Quit, the title bar's X and the system's close — is held once and said; the same within `RUBRAVIEW_CONFIRM_SECONDS` goes on. The idiom is the menu's two presses (`rubraview_confirm_t`), not a modal. The system's close is taken back by `rubraview_pal_window_keep_open`. The folder opened again after a delete or a swap is not held (the file is gone or changed already).
  - Save writes the offset in: `rubraview_subtitle_offset_folded` copies the cues moved by it (none before 0; a cue that would end before 0 ends at 0), marks them moved, and the patch writes every time; the offset is 0 after, the film's remembered one too. Save lights for an offset alone.
  - `putin_refusal` reads the film's name: `.webm` is refused, and `.mkv` / `.mk3d` with a subtitle that is not SubRip or SubStation is refused at the first press. The film's bytes still decide in `putin_begin`.
- Consequences: a film that ends with changes not saved stays on its last picture, saying so, instead of going on to the next. Shutting Windows down is not held.
- Not run: the window closed by Windows itself (log off), drag and drop of a file onto the viewer with changes not saved.
- Later the same day (0.0.81): the hold is a press's, and some goings are not presses. A film's end going on to the next and the slide show are held without arming (`subs_leave_unasked`): it stops there and says so, since nobody pressed and a second tick must not count as a second try. The page bar is held at its press, so the drag's steps after it do not count. A going let through passes the hold for a second (`subs_leave_through`), since one going can come by it twice. A subtitle inside the film is not offered Save in the words.

## D-132: The Boxes In One Order, Everything In The Menu, A Right-Click List (2026-10-14)

- Context: RV-116. The owner asked for the toolbox and the menu grouped and tidied, what the menu lacked put in, and a right click on the picture to offer what fits; then, while it was built: the slide show in the window as it is and its settings a right click away, slides skipped, the list window's R to follow the pointer, a check-all box on the playlist's bar, one Rotate button, hints on the floating boxes, and the titlebar's trigger band as tall as the bar.
- Decision:
  - The toolbox: every profile's groups in one order — going and playing, how it plays, sound, subtitles, view, turning, the file, the window. `Rot <` is gone from the strip (Rotate back stays in the menu, the right-click list and `Shift+R`).
  - The menu box by subject: On top, File (with Up a folder, Save a copy as, This file › Delete for good, Archive ›), Edit (Copy picture, Adjust picture, Save frame, Undo), Go (every turn of page), View (Zoom ›, Turn ›, Next layout, Next shape; the caption "0% real size" was wrong), Playback `when plays` (Seek › by 1, 5 and 30 s and frames; A-B by numbers; Mini player), Sound and Subtitles `when media`, Show (Slide show › with its delay, Toolbox ›), Help, Settings. `when plays` is new: a film, a song or an animation. The node limit is 256. The window nudges stay on their keys.
  - The boxes document gains `context <profile> ... end`: `item` and `group` lines, the toolbox's profile names. The right-click list (`ui_ctxmenu`) opens at the pointer, turned at the window's edges, a line between groups, grey rows by `rubraview_action_state`, Delete asked twice, arrows and `Enter`, `Esc` or a click away to close. A right click that is the subtitle box's, the curve's or the picker's keeps its meaning.
  - While the slide show runs, a right click opens its settings (`ui_slidepop`): the delay typed, or -/+ to the next half second; Cut / Fade / Slide / Zoom (Settings › Viewer › Turning a page); go round or stop on the last; pause, stop, full screen, close.
  - The slide show stays in the window as it is. It skipped: its spreads were counted once when the file opened and its place was its own, so a layout made again (a wide page found, a page left out) or a page turned by hand sent it past spreads; and a slide's time ran while its picture was still being read. Now it follows `layout.count` and the spread on screen (its delay and end kept), and a slide's time starts when its pages are drawn, failed or a film.
  - `copy_frame` (`Ctrl+C`) on a still picture copies the picture from its file at its own size, read in memory of its own.
  - `rubraview_box_hint`: after 0.6 s on a folded anchor's half, the pin, the grip or the seek bar, a line says what a click or a drag does. `Esc` folds an open, unpinned box before anything else (it closed the program).
  - The list window's R keeps the top-right corner under the pointer: the top and the right edge follow, the bottom and the left stay. The bar has a check-all box at its left, over the rows' boxes.
  - The titlebar comes when the pointer is anywhere in its own height: the trigger is never less than 36 px, whatever a settings file kept (they keep every value, so a new default alone would not reach anyone).
- Consequences: the menu is two levels for most things and fits the 4 x 4 box everywhere; a user's own boxes document without `context` lists gets none.
- Not run: a right click on a picture in an archive or a multipage file on the VM; the slide show with an animation or a film among its slides; the page bar's hints at more than one DPI.

## D-133: The Two File Lists Changed Alike, And A File's Own Right Click (2026-10-14)

- Context: RV-117. The owner: the window of its own (thumbnails) and the floating see-through list stay different in that, but take out, change, add and save alike; a right click on a file offers its whole path to the clipboard, Explorer with it selected, out of the list, and deleting it for real — warned and asked, to the recycle bin.
- Decision:
  - One editing path for both: the press, the drag of a row and its release, and the drawing of the bar and of a row's four buttons are shared; the file window's list is `own_window` (its frame moves, sizes and closes it). Both show the same list and the same checks.
  - The bar gains **Add** (both): files picked in the open dialog go to the end, by the rules of a drop (only what the viewer opens, nothing twice). The file window takes drops from Explorer as the floating list does.
  - `context listrow` in the boxes document: Open, Copy the whole path, Show in Explorer, Take out of the list, Delete the file... — for a folder's files or a playlist (not a book's pages). Delete asks in a Windows warning over the window it was asked from, No the default; yes moves the file to the recycle bin (a film playing from it is closed first) and the row leaves the list.
- Consequences: a folder's list changed by Add, Del, a take-out or a delete is from then on a playlist (D-111's rule). A delete from the list is not on `Ctrl+Z` (the file is in the recycle bin).
- Not run: a drop from Explorer onto the file window (no drag could be made on the VM); a delete of the film playing.

## D-134: Subtitles Added, Removed And Timed By Hand, Still Saved In Place (2026-10-14)

- Context: RV-115's leftovers the owner took as recommended: a cue added or taken out, and a time typed, in the subtitle window, saved by D-126's rule (the file copied, only what changed written).
- Decision:
  - The readers note each cue's whole block: a SubRip or WebVTT block through the blank line after it, a `Dialogue:` line with its line end, a SAMI caption from its SYNC to the next (the last one to `</BODY>`), and a SAMI caption's closing empty SYNC.
  - Removed: the cue is kept, unseen, marked; the save takes its block out (and a SAMI caption's closing SYNC). Added: a new cue, marked; the save writes it after the block of the nearest earlier cue of the file's own (or before the first), made from that block with its times and words in their places — its SubRip number, SubStation style, SAMI class, WebVTT settings come along; a last block with no blank line or no line end is set apart first. SubRip numbers are not counted again (only what changed is written; players do not read them). A SAMI caption made after one with a closing SYNC gets its own; one made before the first ends where the next caption starts.
  - A time typed: `start --> end`, read as the files write times (`1:02:03.45`, `02:03,5`, seconds); the end after the start; the cue moves to its place in order.
  - The window: `Insert` (a new subtitle at the film's moment, two seconds, its words typed at once), `Delete` (the ticked rows, or the row last clicked — the viewer's Delete, which deletes the film, does not reach it from here), `Ctrl+T` or a second click on the times.
  - Found on the way: a SAMI file's last caption had its words run on through `</BODY></SAMI>`, so editing that caption since 0.0.75 wrote over the file's closing tags. Its words and its block stop at `</BODY>` now.
- Later the same day, the last two leftovers:
  - SAMI is read in the order of its SYNCs: a caption whose new time puts it out of order with the captions that did not change is taken out where it was and written again; every SAMI piece written in (a caption, its closing empty SYNC, apart) goes before the first SYNC still in the file that starts later (a caption) or not earlier (a closing SYNC), so the SYNCs stay in order of time. Two captions made to overlap cannot both show in SAMI: the earlier one's closing SYNC ends the later one there.
  - A save whose file is in a code page that cannot hold a character typed is not refused outright: it says so and a second Save within six seconds writes the file as UTF-8 with its mark (`EF BB BF`); the file before is kept as always.
- Not run: SAMI with several languages added to on the VM; a WebVTT or SubStation file on the VM (unit tests only).

## D-135: The Arrow Keys Move A Zoomed Picture (2026-10-14)

- Context: the owner asked for the arrows to move the picture when it is zoomed. They had nothing on a picture since 2026-09-09 (taken off page turning on request); a film uses them for seeking and the volume.
- Decision: `[view]` binds Left, Right, Up and Down to `pan_left` / `pan_right` / `pan_up` / `pan_down`. A press shows what lies past the window's edge the arrow points to, a seventh of the window, never more than is hidden (`rubraview_pan_key_delta`); along an axis where the picture fits nothing moves, and nothing else happens (the arrows still turn no page). The picture's place is the spread's bounding box, turned as shown. In the webtoon strip Up and Down scroll it. A film's and an animation's arrows (`[media]`) are as they were.
- Not run: the webtoon strip and a two-page spread on the VM.

## D-136: At The First And Last, And What Came With It (2026-10-14)

- Context: the owner, one request after another while 0.0.85 was being finished: a setting for Next past the last page and Back before the first; the cursor gone over the settings window; the layout tile to say which layout it is; read ahead and keep behind up to 99; a choice in the settings that went backwards when clicked; two pages at 100 % cut at the edges; `.` and `,` for the next and previous file; the keys page to say what each action does.
- Decision:
  - **Settings › Files › At the first and last**: `say` (the default: "this is the last" / "this is the first", it stays), `next` (the next or previous archive beside it, or, for a folder, the next or previous folder beside it in natural order that holds a file the viewer opens; going back opens it at its last page), `round` (to the other end of what is open). An archive used to run into the next one silently, and a folder said nothing; both now follow the setting.
  - **The cursor**: it was hidden with `ShowCursor`, which hides it over every window of the program — the settings window too, where moving it brought nothing back; and the slide show hid it after 1.5 s even over its own settings or a list. Now `WM_SETCURSOR` hides it in the main window only, and not while the slide show's settings, a right-click list, the subtitle chooser, the settings, a panel, the list window or an open box is there.
  - **Layout**: its short names are the settings' (`single`, `dual`, `dual-rtl`, `book`, `book-rtl`, `webtoon`, `comic`, `comic-rtl`); the strip's name row says a hovered tile with its state ("Layout: book-rtl", "Crisp: on"); a change says "book-rtl: the cover alone, then two pages, right to left"; resting on the layout's tile says the same.
  - **Read ahead / keep behind**: up to 99 each (the memory cap up to 16 GB). Found on the way: every update of the ring set it back to 2 and 1, so the two settings never held; now the reader's numbers are kept and a fast slide show only widens them. The ring is cut to what the memory budget holds, nearest first, so no page is read only to be evicted.
  - **A choice clicked** goes on; only its `<` goes back (the left quarter of the row went back, and the word lay in it).
  - **Two pages at 100 % cut**: a frameless window maximized is made larger than the screen by its frame on each side, and the canvas ran off the screen; the client is the monitor's work area now (full screen is left as it is).
  - **`.` and `,`** are Next and Back too; in a multi-page file they turn its pages and go on past its ends; an animation's and a film's frames keep them.
  - **The keys page** says what a row's action does when the pointer rests on it, from `src/core/action_words.c`, generated from `check-actions.py`'s words (the same as the manual's key tables).
- Not run: `round` with more than one spread on the VM (the VM's layout was two pages); the strip's hint on a non-layout tile; a TIFF's pages with `.`.

## D-137: The Toolbox In Framed Groups; The Menu Box Below The Title Bar; 0 And 6 (2026-10-15)

- Context: the owner: the box button put the menu box where the title bar comes; the toolbox's groups each took a row of their own; "레이아웃이 바뀌어도 툴바 위치는 바뀌지 않도록"; the title bar's Size button sized from the bottom right while it sits top right; 4 is real size, so `Ctrl+0` can be `0`; Stretch from `Ctrl+1` to `6`.
- Decision:
  - **Groups wrap as words**: a group goes on the line while the line's buttons stay within `strip_columns` (8 as it comes; the grip changes it), else on the next; a group longer than a line has lines of its own. Each group sits in a rounded frame of its own colour (`COLOR_GROUP_FILL`, `COLOR_GROUP_BORDER`), `group_pad` inside it, `group_gap` between frames. The strip is as wide as its widest line, never narrower than one frame of `strip_columns`. The name's rows are measured on that width.
  - **An open box keeps its side**: `open_side` is decided once the grid is open (below the anchor when it fits, else above) and kept while it stays open; grown past the window's edge it is pulled inside instead of going to the other side. Folded or detached, it is forgotten.
  - **The menu box's home** is `gutter` below the title bar's height (`home_top`); a first start puts it there too (its share is taken from that place). Saved places are kept.
  - **Size** in the title bar is `SC_SIZE | WMSZ_TOPRIGHT`: the button stays under the pointer.
  - **Keys**: `0` → `zoom_reset`, `4` → `actual_size` only, `6` → `fit_stretch`; `Ctrl+0` and `Ctrl+1` are free. A keymap.ini of the reader's own keeps what it says.
- Consequences: the toolbox is shorter (an image's: five lines) and, with lines of two groups, a little wider than eight buttons. On the VM, changing the layout with `B` and with the layout tile did not move the toolbox in this build (nor did a saved place from an older test); the side kept is for what could still move it (a grid that grows past its side). Not seen: the reader's own case of it moving.

## D-138: The Menu From The Title Bar; A Slimmer Toolbox; Panels With An X; Export By Format; Size In Pixels (2026-10-15, RV-118)

- Context: the owner: two floating boxes are one too many — a three-line button at the title bar's left opens the menu under the bar, which stays; the unimportant toolbox buttons to the menu; the three zoom buttons one (real size, 20 % to 200 % by twenties; real size written on two rows); the fit buttons one going round, stretch with them; the toolbox's V draggable as T is, and both explained by a hint; Setup to the menu, the pin and full screen gone from the toolbox (the title bar has them); Export showing only its format's options; an X on Edit and every other panel; Edit's changes shown on the picture; Resize in pixels too. Answers (plan RV-118): the tiles as proposed, Smart in the fit round, one release.
- Decision:
  - **The menu**: with our title bar the menu box is headless: no anchor, pin or grip, no hover; `RUBRAVIEW_TITLEBAR_MENU` (three lines, the bar's left end, lit while open) and `Tab` open and fold it; its grid's top-left is (0, the bar's height); the bar is kept shown while it is open and shows the breadcrumb as its caption; a leaf chosen, `Esc`, or a click anywhere else folds it (that click does nothing more). With Windows' frame (Frameless off) there is no bar of ours, and the M box is as it was.
  - **The toolbox**: an image's: Prev, Next, First, Last | Zoom, Fit, Rotate | Layout, Slides | Open, List, Files, Info | Edit, Export — two lines of eight. The others likewise: Top, Setup and Full out of every profile; Zoom- / Zoom+ / Fit W / Fit H / Copy to / Move to / -10 / +10 / Strip / Flip out of those that had them (all in the menu, which also gains Next zoom and Next fit). `fit_cycle` goes window, width, height, smart, stretch, window; its tile names the fit in force. The zoom tile reads "Real\nsize" at 0 %. A caption longer than five letters is drawn smaller.
  - **Anchors**: either half of the toolbox's anchor is a handle (a press without a drag is the half's own click); the hints of T and v show open or folded.
  - **Panels**: an X a row tall at the title's right end closes any panel; the click outside and `Esc` still do.
  - **Export**: rows by `rubraview_export_rows(format, the file's name, lossless)` — JPEG: quality, progressive; PNG: level, colours; WebP: quality unless lossless, lossless, effort; privacy for JPEG, PNG, WebP, TIFF; GIF, BMP, ICO nothing; "as the file" is the file's own format. The panel is built again when the format or WebP's lossless changes.
  - **Adjust**: `Size: W x H px...` under Resize % opens a two-field box like A-B's; digits only, five at most; the other side follows the shape while the aspect lock is on (`rubraview_edit_resize_pair`); Enter sets the resize and rebuilds the panel. The preview, if not had at opening (a page job under way, the page not yet in), is asked for again every half second, twenty times at most; blur and sharpen are compared too.
- Consequences: the M box's place in layout.ini is still read and kept, for a frame of Windows'. The menu cannot be pinned open under the bar. The edit preview was seen working on the VM for a still JPEG and PNG (exposure, blur); what the owner saw not change was not reproduced, and the retry covers the one way found for it to be missing.

## 2026-10-15: D-139 No "On top" in the menu

- Status: Accepted (owner 2026-10-15: "메뉴에서 On top 항목을 지워주세요. 타이틀바에 이미 pin이 있어요")
- Decision: the menu's top-level "On top" entry (D-17) is removed; always on top toggles from the title bar's **Pin** and `Ctrl+Shift+T`.
- Also (owner, the same day: "그림 모드에서는 마우스 휠 버튼으로 이미지 다음/이전 장으로 넘어갈 수 있으면 좋겠네요"): the plain wheel turns pages on a picture or book page that fits (`wheel_turns_pages` in the pointer context: not media, not the strip), away from you the one before, reversing 2026-09-09. Fit Width still scrolls; a picture zoomed past the window takes nothing from the wheel, so one notch never nudges here and jumps there.
- Also ("Fit 버튼도 ... 현재 어떤 옵션인지 알려주게 ... 아이콘도 현재 옵션에 따라"): `fit_cycle`'s icon follows the fit (FitPage, Switch, Sort, LightningBolt, FullScreen; real size in words) over its short name; its hint is `rubraview_fit_tile_hint`, one string a fit.
- Consequences: the menu's top level starts with File. The same day (owner: "메뉴 버튼의 Information은 Info로 줄여주세요") `toggle_info` reads "Info" in the menu and in the right-click lists too, as on the toolbox.

## 2026-10-15: D-140 The crop typed; A-B's point just taken wins

- Status: Accepted (owner 2026-10-15: "크롭에 대해 좌표 입력해서 해당 위치를 자르거나, 그걸 이미지 위에서 지정할 수 있게 해 주세요"; "구간반복 시 뜨는 박스의 오른쪽 위에 취소를 위한 x 버튼 ... 현재 위치를 A 또는 B에 넣을 수 있어야 하고 ... 방금 입력한게 A 일때는 영상 마지막까지, 방금 입력한게 B일때는 영상 처음부터")
- Decision:
  - **Crop**: Adjust's second page has `Crop: x, y  W x H px...` under Crop ratio; it opens the pixel box with four fields (X, Y, W, H), `rubraview_edit_crop_set` takes them (the ratio becomes free; a corner outside or a size under 1 px is refused; a size past the edge is cut at it). The drag on the picture, which was there, now fills the box while it is open and the button says the cut.
  - **A-B**: `rubraview_ab_take(a, b, t, is_a, duration)`: a new A at or past B moves B to the end, 0.1 s short so the loop comes before the file ends (unset when the length is not known); a new B at or before A, or with no A, moves A to 0. It serves `[`, `]`, the A-B tile and the box. The box gains **A = now [** and **B = now ]**, a click on a half of it chooses that field, and an X at its top right; the pixel box has the X too.
- Consequences: "B must come after A" is no longer said for `]`; it remains for two typed numbers out of order, where neither was just taken.

## 2026-10-15: D-141 The wheel brings up the filmstrip

- Status: Accepted (owner 2026-10-15: "휠 돌려서 이미지 사이 이동할 때, 하단에 썸네일들이 좌우로 움직이면서 선택 가능하게 해 줘요")
- Decision:
  - A page turned by the wheel (D-139) shows the filmstrip over the foot of the picture for 2.5 s, then a 0.4 s fade; the pointer on it holds it. It does not move the picture up, as `F4`'s strip does.
  - It slides to keep the page in the middle (`rubraview_filmstrip_centre_offset`, `rubraview_filmstrip_glide`: most of the way in a fifth of a second); a short row sits in the middle (`rubraview_filmstrip_lead`).
  - Thumbnails come from a second thumbnail thread, the picker's kind (`strip_thumbs`, a generation per book or folder): a folder's file by its path, a page in a book by its bytes, three a pass and none far inside a streamed block; until one comes, the page itself if it is in memory, else its number.
  - A click on a thumbnail goes to its page (`rubraview_filmstrip_index_at`, `spread_for_chosen_page`); the wheel over the strip goes a page a notch. The toolbox drawn over it keeps its clicks.
  - `F4` keeps the strip up as before, now the same drawing, and kept across books (it was let go at each open).
- Consequences: the old strip drew only pages already decoded, stretched to the cell; thumbnails now keep their shape (the thread's are cut to a square, as the picker's are).

## 2026-10-15: D-142 A-B in hours, minutes and seconds; Crisp and Grid explained

- Status: Accepted (owner 2026-10-15: "구간반복 화면에서 시:분:초 각각 입력할 수도 있게 해 주세요"; "crisp와 grid에 대해 아이콘 위에 커서 올리면 툴팁으로 안내메시지 나오게 해 주세요")
- Decision:
  - The A-B box shows each point as three fields with their units: hours (whole), minutes (whole, past 59 adds up), seconds (up to three decimals). `rubraview_timecode_split` fills them (rounded to the millisecond); `rubraview_timecode_join` reads them (all empty: the point unset; a field not a number: refused, the box stays). `Tab` / `Shift+Tab` go through the six fields, `Up` / `Down` to the same field of the other point, `:` to the next field, a click to the field under it; a field just come to is replaced by the first digit. `[`, `]`, Left / Right, `\\` and the buttons work as before.
  - `rubraview_action_hint`: Crisp (`toggle_nearest`) and Grid (`toggle_pixel_grid`) say what they do when the pointer rests on them, in the menu or on a tile.
- Consequences: the box is taller (a title line clear of its X). One free-form timecode field is gone; `rubraview_parse_timecode` stays for the command line and the tests.

## 2026-10-16: D-143 The information bar says the name and the place

- Status: Accepted (owner 2026-10-16: "인포메이션 바에서 파일이름과, 현재 몇번째 파일인지 보이게 해 주세요. 좌측부터 파일 이름(너무 길면 처음부분부터 보여주다가 중간에 ...으로 그리고 마지막 10자 정도 보여주게.) 그리고 우측부터 현재 목록/폴더 안에 몇개의 파일이 있고 그중 몇번째인지, 해상도와 그밖의 주요 정보를 보여주게 해 주세요.")
- Decision: the bar's left is the page's name, whole when it fits, else the longest beginning that fits, "...", and its last ten characters (UTF-8 code points, `rubraview_middle_ellipsis`); its right is `rubraview_osd_right`: "3 / 120", width x height and zoom for a picture, the format in capitals from the name, the size ("2.4 MB"), "shown reduced". The size is read once a page. A film or a song shows its place, format and size.
- Consequences: this reverses 2026-09-29's "the name and the position are on the toolbox's line: not shown twice" for the bar, which is off until asked for.
- Amended (owner, the same day: "정보 막대에서 제일 오른쪽은 15/15의 순서고, 그 왼쪽이 해상도여야 할 것 같아요. 그리고 파일 형식은 불필요해서 정보막대에는요. 배율은 없애주세요."): the right half reads "shown reduced", the size, the resolution, and the place at the very right; the format and the zoom are left out.

## 2026-10-16: D-144 Subtitle sync by tenths, reset, and in sight

- Status: Accepted (owner 2026-10-16: "자막 싱크 보완 처리해 주세요", the three items offered: a reset, a finer step, the value in sight)
- Decision: `Shift+Z` / `Shift+X` (`subtitle_earlier_fine` / `subtitle_later_fine`) move the subtitles 0.1 s; `Alt+Z` (`subtitle_sync_reset`) sets the offset back to 0; Subtitles in the menu lists "Earlier 0.1 s", "Later 0.1 s", "Sync reset". The offset is kept to the millisecond (`rubraview_subtitle_shift`), so mixed steps come back to 0 exactly. The playback line under the toolbox's bar ends with "sub +0.6 s" while it is not 0; the OSD says "subtitles in the file's own time" at 0.
- Consequences: the keys are the implementer's picks (`Ctrl+Z` is Undo, so Alt+Z for the reset). The offset is still per film and written into the file only by the list window's Save (D-131).

## 2026-10-16: D-145 The open-file screen's details, sorting, Recent, and folders kept out without a limit (RV-119)

- Status: Accepted (owner 2026-10-16, the request and four answers: toggle "Select all" until all are picked; resolution and length read for rows on screen; Recent as a place on the open-file screen and in the menu; skip folders "무한히 넣을 수 있게 합시다. 고정이 아니라 더 많이요"). Plan `docs/plans/archive/2026-10-16-RV-119-picker-recent-details.md`.
- Decision:
  - **Toggle**: the action bar's last-but-one button reads "Select all" (picks every file, not folders, and turns Individual on) until every file is picked, then "Deselect all".
  - **Columns**: Name, Type (the extension), Size, Created, Modified, Resolution, Length. A narrow window lets Created go first, then Resolution, Length and Type, keeping 240 px for the name.
  - **Resolution, length**: carried back with each thumbnail (the thumbnail result gains the source's width, height and seconds); for a file, the shell's property store (image or video size, media duration); for a page in a book, WIC's frame size. Only rows asked for — those on screen — have them.
  - **Sorting**: a click on a column's name sorts by it rising, again falling (▲ / ▼ on the head); `..` and folders stay first; names break ties; a file whose resolution or length is not read yet goes last either way and does not move when it is read until the next click. Kept in `files.picker_sort` (as_folder | name | ... | length) and `files.picker_sort_desc`; `as_folder` is the folder's own order (Settings › Files › Sort by).
  - **Recent**: `::recent`, crumbs PC > Recent, `..` the PC page, a chip first after the favourites; File › Recent › "All recent files..." (`open_recent_list`). Thirty at most, newest first, each read from the disk; one gone is left out; no star.
  - **Folders kept out**: one hidden setting `recent.skip_dirs`, folders joined by '|'; a `table skip_dirs` on Files shows them and "+ add a folder" (Enter types or browses, Delete takes off). The settings document gains `hidden` (kept, saved, no row) and the window tables by name. A held path emptied is written empty, so the eight `skip_dir_N` moved into the list at start-up stay empty.
- Consequences: a stored sort applies to every folder and to Recent alike; "as_folder" in Settings goes back to the folder's order. The ▲ / ▼ glyphs need a font that has them (Segoe UI does).

## 2026-10-16: D-146 A copy or a move to any folder, made when new; the folders kept out taken off by mouse

- Status: Accepted (owner 2026-10-16, two answers: "다른 폴더 고르기 추가" and "줄 끝에 ✕ 추가"; the keys, the 0 and the second Enter are the implementer's picks).
- Context: the owner asked whether a new folder could be made right where files are copied or moved, and whether a folder kept out of Recent could be deleted; it could only be with Delete, and a copy or a move went only to the folders set in Settings.
- Decision:
  - **Another folder**: `copy_to_other_folder` (Shift+F6) and `move_to_other_folder` (Shift+F7), also Menu › File › This file and the picture's right-click list, and F6 / F7 while no folder is set for them, open the text box in the middle of the window holding the last folder used (`curation.other_dir`, hidden) or else the F6 / F7 folder. On the open-file screen, Move to / Copy to take `0` for it beside 1-9, and with no numbered folder set open it at once.
  - **New folders**: Enter on a folder that is there puts the files in it; on one that is not, it says so (`rubraview_target_folder_step` OFFER) and a second Enter on the same path makes it (MAKE); a file of that name is refused. Ctrl+O browses with the system's folder dialog.
  - **X**: each folder kept out has an X at its row's end; a click takes it off, as Delete does.
- Consequences: no folder is made by one stray Enter; a relative name is taken from the picker's folder or the file's own.

## 2026-10-16: D-147 Version 1.0.0; the Store's version is the program's

- Status: Accepted (owner 2026-10-16: "버젼도 이제 1.0.0 으로 올려주세요. MS Store 공개를 위해서요", and the answer "1.0.0.0, 그대로").
- Context: D-122 gave the Store package the program's version with the first number one higher, because the Store refuses a first number of 0 (0.0.70 was 1.0.70.0). At 1.0.0 that rule would give 2.0.0.0. RV-113's item (6) left this rule to be decided once the program reached 1.0.
- Decision: the program is 1.0.0. From 1.0 on, `scripts/package-store.sh` gives the Store package the program's own version plus a fourth number, 0 (1.0.0 is 1.0.0.0). A 0.x version keeps the old rule. The installer and the Store package are made with rubrapack 0.47.0, and `vendor/rubrapack/VENDORED.md` notes v0.47.0. Its files are the same as before.
- Consequences: the Store and the program show the same number. If a test package of 1.0.70.0 to 1.0.89.0 had been submitted to Partner Center, it would refuse 1.0.0.0 as lower; the owner says none was.

## 2026-10-16: D-148 The RFCs are private

- Status: Accepted (owner 2026-10-16: "readme에서 rfc 내용과, 공개된 rfc 등도 회수하고 삭제하고 비공개 폴더로 옮겨줘요", and "히스토리까지 지움").
- Context: D-69 made `docs/rfc` public with the source, the manual and the decisions.
- Decision: `docs/rfc` moves to the private repository; the READMEs no longer link to it; the allowlist no longer lets it in. The public history was rewritten without `docs/rfc` and pushed again, main and every tag; the releases stay on their tags.
- Consequences: every public commit hash changed; a clone made before 2026-10-16 still holds the RFCs. Comments in the source that name an RFC section stay, as references.

## 2026-10-16: D-149 Language packs: Korean, Japanese, Chinese and Persian

- Status: Accepted (owner 2026-10-16: "프로그램 자체의 한글팩 만들어 주시고 언어 바꾸는거 가능하게 해 주세요", "그리고 일본어 중국어 대만 등 주요 국가 번역도요", "번역에 페르시아어 추가해 주세요"; the plan's answers: files only, `auto`, the information window copies what is on screen, one release when all is done; the English text as the key is the implementer's pick).
- Context: every text the viewer showed was English; Korean existed only in Explorer's menu, the README and the installer (RV-114).
- Decision:
  - A pack is `lang/<code>.txt`, UTF-8, `"English" = "translation"` a line, `context "English" = ...` where one word needs two (`toolbox`, `menu`, `choice`, `unit`, `plural`). The English text is the key; a text not in the pack stays English (`rubraview_tr`, `T()`, `TU8()`, `TC()`, `N_()`).
  - A pack is data from outside: a line whose printf conversions differ from the English is refused, sizes are bounded, a broken line is skipped (`rubraview_lang_load`).
  - None is built in: `rubraview.exe` alone is English; the zip, the MSI and the Store package carry `lang\` beside it. The settings folder's `lang\` is read first.
  - `general.language` = `auto` (Windows' display language; `zh-TW/HK/MO` take Traditional, other Chinese Simplified) | `en` | `ko` | `ja` | `zh-hans` | `zh-hant` | `fa`, shown by each language's own name; a change applies at once (menus, the settings window, the title).
  - Wide characters take two cells of the settings grid (it already counted them so).
  - `scripts/check-lang.py` (in the project check) collects every marked text, the settings document's labels, choices and units, the boxes' captions and the actions' descriptions, and fails when a pack lacks one or holds one that is gone. 2018 texts at 1.1.0.
- Consequences: a text written later stays English until the packs get it (the check says so). Not translated: settings.ini and keymap.ini, command-line output and the diagnostics, file contents, format names, licence texts, the file dialogs' filter names. Persian is right-to-left; the windows are not mirrored, DirectWrite orders the letters. Explorer's own menu keeps its English and Korean.

## 2026-10-17: D-150 FFmpeg and language packs where the program's folder takes no files

- Status: Accepted (owner 2026-10-17: "개인적으로는 다른 위치의 파일들을 쓸 수 있게 하는게 좋을 듯 해요", "1. 외부 디렉토리 지정 2. rubraview 설치 디렉토리에다가 지정한 파일들 복사 이렇게 두가지 방식 다 쓸 수 있게 하면 좋겠어요. 언어팩도 마찬가지.", "언어팩은 그냥 import 만 가능하면 좋겠네요."; plan `2026-10-17-ffmpeg-folder`).
- Context: the Microsoft Store's package lives in a folder nobody can add to, and Program Files takes no files without an administrator; FFmpeg's DLLs and a user's language pack were found only beside `rubraview.exe`.
- Decision:
  - FFmpeg is looked for in three places, each taken whole (all five, loaded in the order they import one another, by full path, their own imports from that folder and System32) or not at all: beside the program, then the settings folder's `ffmpeg` (where Import puts them), then `video.ffmpeg_dir` (a folder the user names; quotes, spaces, trailing separators and `%VARIABLES%` taken off).
  - Settings › Video › **Import FFmpeg's DLLs...** asks for the folder (the zip's `bin`), checks the five of this build's version are there, and copies them beside the program, or into the settings folder's `ffmpeg` when that fails; each file is copied beside its target under another name and then takes its place.
  - Settings › General › **Import language packs...** copies chosen packs, only ones named as a language this build offers and that read, to `lang` beside the program or the settings folder's `lang` (already read first), and reloads the language. No folder setting for packs (the owner).
  - A failed FFmpeg load is no longer final for the run: the places are looked in again when they are given again (a setting changed, Import), so Import works without a restart while none is loaded. One that loaded holds for the run.
- Consequences: the user's own folder is trusted as the user's own settings file is. Messages about a missing FFmpeg point at Settings › Video › Import instead of "beside rubraview". A deny-write ACL on the program's own file stops the viewer opening at all (seen with 1.2.0 too); Program Files' ordinary permissions do not.

## 2026-10-17: D-151 Open with and the right-click menu chosen per extension; the Store's copy knows itself

- Status: Accepted (owner 2026-10-17: "다 추천대로 진행." — RFC-0005's answers, RV-113 (2)).
- Context: each extension had one switch for both parts (D-86), and the owner asked for Open with and the menu to be chosen apart (RFC-0005 §9). The Store's copy offered a registration that writes into its package's own registry, which Explorer never reads (D-122).
- Decision:
  - `shell.ext_<ext>` keeps meaning Open with; `shell.menu_<ext>` is new. A settings file without it starts the menu box as that extension's Open with box was. The two switches over all stay and take their column back when off; a column with nothing ticked is that part taken back.
  - The settings document's `pair` line puts a second toggle in the row before it (`pair shell.menu_jpg "Open with" "Right-click menu" = true wired`): a hidden setting the first one points at (`rubraview_setting_def_t.pair`). Left and right choose the box, Space ticks it, a click ticks the one under it; the box chosen stays from row to row.
  - Registration takes two lists: the file types for the Open with column, the classic menu and Windows 11's menu (its "On" list, already read by `rubraview_menu.dll`) for the menu column. As an administrator: `--menu-types=` beside `--types=`. The menu DLL is not changed.
  - `rubraview_pal_process_packaged` (GetCurrentPackageFamilyName): run from a package, Settings › Explorer shows **This copy** as the Store's, the Register / Remove / Remove everything rows say Windows has the types from the package instead of writing anything, and Settings › General shows the settings file in the package's LocalCache when it is there.
  - The settings store holds 224 values (was 160): 191 now.
- Consequences: RFC-0005 is accepted with every proposal (its §12); its text viewer (§1–§8, RV-121) is next. Windows 11's menu in the Store package (RV-113 (3)) needs no change in rubrapack, which has `[msix-extension] kind = "context-menu"`; it is still to do.

## 2026-10-17: D-152 The text viewer

- Status: Accepted (owner 2026-10-17: "텍스트뷰어부터 완전히 다 구현해 주세요" with the four ways of line breaks, the indents, auto-scroll whose speed changes while it runs, easy size and line spacing; "워드랩을 창 크기에 맞게 할 것인가 아닌가도 툴바에서 바로 조작할 수 있게요. 아이콘은 word wrap 으로 2줄로 표시"; RFC-0005 as proposed).
- Context: RFC-0005 §1–§8, accepted with D-151; the owner then gave the line breaks as four ways instead of the RFC's three.
- Decision:
  - `.txt`, `.log`, `.nfo`, `.md` are pages (`TEXT_FILTER`), in a folder and in an archive; a text page is read by `text_open` (main.c) when it comes on screen, as a film is, never by WIC.
  - `src/core/textenc.c`: the encodings of §2.1 and the guess of §2.2 — a mark, the 7-bit escapes, UTF-16 by its zeros, strict UTF-8, then CP949, Shift_JIS, EUC-JP, GBK, GB18030 and Big5 scored on the first 64 KB by where their characters fall (Hangul, kana, the hanzi rows each script uses most, their punctuation), a byte that is no character costing ten; within 5 % the reader's language wins; nothing scoring is CP1252 (CP437 for a `.nfo`). Decoding: UTF-8 and UTF-16 in core, the rest through `rubraview_pal_text_decode` (a code page). By hand (`E`) is kept per file in the history file (`path|encoding`).
  - `src/core/textview.c`: the breaks — `keep`, `one_line` (laid out in pieces of about 4 KB cut after a sentence), `no_blank`, `paragraphs` (a sentence's end and a run of breaks longer than the file's commonest; else a sentence's end before an indented line; else any sentence's end) — and `trim_lead`; a space joins lines unless either side is Han, kana or CJK punctuation. Each paragraph keeps where it began in the file, so the place survives a change of breaks and is what is kept for the next time.
  - Drawing: a paragraph is a DirectWrite layout (`rubraview_pal_text_block_*`), made when in view, freed past 400; heights are guessed until laid out; the view is held by its top paragraph and an offset, so heights found later do not move what is read.
  - Auto-scroll moves by the time since the last pass, in quarter pixels, its speed in lines a minute (`text.autoscroll_speed`, 2–600); `A` starts and stops it; while it runs `+`/`-`, the wheel and the arrows are its speed; a click stops it.
  - Keys in a `[text]` section consulted first on a text page; a `text` toolbox (Auto, Slower, Faster, A-, A+, Lines-, Lines+, Breaks, Indent, Word wrap, Find, Encoding) and right-click list; Settings › Text. Find reuses the typed box (`rename_is_find`).
  - The settings document holds 384 lines (was 256).
- Consequences: no editing; files over 64 MB refused; the HKSCS rows read as Big5; a heading line with no sentence's end is joined to the paragraph after it in `paragraphs` (as the owner put it: "only when").

## 2026-10-17: D-153 Windows 11's menu in the Store's package

- Status: Accepted (made by the rubrapack session at the owner's request; shipped in 1.5.0).
- Context: RV-113 (3), D-122: the Store's package had no Windows 11 right-click menu; the program's own menu DLL reads its items from a registry key the registration writes, which a package cannot use.
- Decision: `resources/distribution/rubraview-store.toml` declares seven top-level `[menu.*]` items, M1Open to M7Print, as `MENU_ITEMS` in `src/core/shellreq.c` has them, each `on` the extensions of its kinds (`SHELL_GROUPS`), each running `<flag> --whole %*` (Browse for one file only). rubrapack puts its own `rubrapack_menu.dll` into the package (`desktop4:FileExplorerContextMenus`, a `SurrogateServer` class per item); rubraview's code is unchanged. `scripts/check-store.py` compares each item with `MENU_ITEMS` and `SHELL_GROUPS`. IDs are letters and digits only (an "_" made Windows refuse the package, 0x80080204).
- Consequences: on the VM (Windows 11 26200, Korean, a test copy) each kind showed its own items in Korean under one entry, and Open on two pictures started one viewer with both. The real package's entry is "Rubraview", beside the MSI's own "Rubraview" sub-menu on a machine with both. Unlike rubraview's DLL, rubrapack's has no list-file fallback: a selection whose command line passes 32K characters does not start the viewer.

## 2026-10-17: D-154 Two copies on one machine

- Status: Accepted (owner 2026-10-17: "예 그렇게 해 주시고. 중복 설치 방지도 마찬가지로 루브라팩에도 요청해 주세요." — to tell the reader, and not to add a second Windows 11 menu).
- Context: the MSI's copy (with its own Windows 11 menu, D-116) and the Store's (with rubrapack's, D-153) can both be on one machine: two "Rubraview" entries in Windows 11's menu. A Store package runs nothing of ours when it is installed, so this is known only when a copy runs.
- Decision:
  - `rubraview_pal_shell_other_copies`: the MSI's copy by its UpgradeCode (`MsiEnumRelatedProducts`, msi.dll loaded by name); it is this copy when its registered menu (HKLM) runs this program, or, with nothing registered, when this program is under Program Files. The Store's copy: a package of the user's (`AppModel\Repository\Packages`) whose folder holds rubraview.exe, not the MSI's own menu package and not this one. No Store identity value is in the code.
  - Settings › Explorer › **Other copies** says which there are, and what follows. Where two Windows 11 menus show (the MSI's registered and a Store copy present), the reader is told once (`shell.told_twice`).
  - Registering for every user while a Store copy is there writes the classic menu and takes this copy's Windows 11 package off (`RUBRAVIEW_SHELL_MENU_CLASSIC_STORE`; exit code 5, which the installer's run counts as done): the Store's menu is the one.
  - rubrapack was asked (2026-10-17) for a list-file fallback in its menu DLL (the 32K limit of D-153) and for help at install time (a warning in the MSI, a menu item hidden by a marker); its answer decides what follows.
- Consequences: an MSI copy registered before a Store copy came keeps its Windows 11 menu until it is registered again or removed; the reader is told. A zip copy is not found by the others.

## 2026-10-17: D-155 Edit as steps; effects, colour, resize, information, batch, classify, settings explained

- Status: Accepted (owner 2026-10-17, plan `edit-selection` confirmed: rectangle and ellipse, a hard edge with an optional feather, the old file to the Recycle Bin, archives written again; then, the same day: resize, batch and colour after IrfanView's screens "그대로 배끼지 말고", effects "MIT로 구현하기 적절한 알고리즘", EXIF as far as IrfanView's page, image classification, settings tooltips and three Defaults, a font from Windows' own dialog — option A, the face only).
- Context: Edit was a set of sliders over the whole picture with one crop; the information window, the resize and the batch had less than IrfanView's; curation moved one file a key; some settings needed explaining.
- Decision:
  - Edit is a list of steps (`rubraview_edit_steps_t`), each on an area (all, a rectangle or an ellipse with a feather), applied in order for the preview (scaled) and the save (whole); undo / redo / reset until saved.
  - Effects, colour balance, automatic colour, the linear-light resampler and the batch's frames and watermark are our own code in core (MIT), tested on the host.
  - The batch window's choices are the run's command line (one option a token), so what is remembered, saved and run is one thing, read back by the same parser.
  - Classification sets files aside in memory and moves them at once on Copy all / Move all.
  - Settings help is text in the settings document (`help` lines), translated like the rest.
- Consequences: the preview of a pixel-sized effect or a feather is scaled to the preview's size; undo after a classification reaches the last 64 files; no text overlay in the batch (the PAL draws text on the screen only); DPI is not written into a resized file.

## 2026-10-17: D-156 Two copies, settled with rubrapack 0.48.0

- Status: Accepted (owner 2026-10-17: "둘 다 예" — the Store's identity in the public MSI, and Developer Mode on the VM to test the Store's package).
- Context: D-154 asked rubrapack for a list-file fallback and for help at install time; rubrapack 0.48.0 has `[menu.*] list`, `[menu.*] hide-when`, `[other-copy.ID]`.
- Decision:
  - The Store's seven items carry `list = "--paths-temp --paths-from=%L"` (a selection past 32K characters goes in a file the program reads and deletes) and `hide-when = "HKLM\\Software\\Rubraview\\Win11Menu\\Registered"`.
  - The program writes that value when its own Windows 11 menu package is registered (for every user) and deletes the key when it takes the package off; the MSI deletes the key at removal (`[registry.MenuMarkerGone] remove = true, on = "uninstall"`).
  - The MSI asks before a first install when the Store's copy is there (`[other-copy.Store]`, warn). The identity is given at build time from `RUBRAVIEW_STORE_IDENTITY` (`scripts/package.sh`, as `package-store.sh`); without it the table is left out and the build says so. The repository holds no identity.
- Consequences: one Windows 11 menu in every order seen on the VM (2026-10-17, Windows 11 26200, Korean, the Store package signed by a test certificate in its publisher's name): the MSI's registered first, then the Store's added — one entry, the MSI's; the MSI removed — the marker gone, the Store's entry shows; the MSI installed beside the Store's copy — the installer found it ("other copy Store is installed ... going on without a window" under `/qn`), and its registration, run as SYSTEM, cannot see a user's Store package, so it registers the MSI's menu and the marker hides the Store's: one entry again. From Settings, run as the user, D-154's step-aside still applies. The MSI built with the identity carries the Store's name and publisher.

## 2026-10-17: D-157 The text viewer's leftovers; words and a DPI written by the batch

- Status: Accepted (owner 2026-10-17: "RV134와 123 둘 다 진행 바랍니다").
- Decision:
  - RV-123: `text.autoscroll_mode` (smooth / pages) and `text.autoscroll_page_seconds` (a screen less one line, every N seconds; + / - change N); `text.headings` (off as it comes: the owner's rule as said joins a heading to the text after it) — a line of at most 30 columns, no sentence's end, a blank line before (or the file's start) and after; the information bar: the encoding and "% through"; `RUBRAVIEW_SHELL_TEXT` (`txt;log;nfo;md`) on Settings › Explorer (both columns off) and in the Store's package (Open and New window items, file types).
  - RV-134: the batch's text is drawn by GDI (grayscale antialiasing, white on black: one channel is the cover) into a GRAY8 mask, made a coloured layer with a soft shadow in core, and laid on as the watermark is but as wide as the picture allows; `rubraview_export_options_t.dpi` set on the WIC frame; a JPEG turned losslessly gets the DPI in its JFIF block, or is written the ordinary way when it has none.
- Consequences: seen on the VM — ISO-2022-KR, Johab, HZ and CP437 read (the first and the third guessed by themselves); headings alone; a screen every 2 s; "UTF-8, 27 % 읽음" on the bar; yellow words at the bottom right of each picture; 300, 200 and 150 dpi read back from JPEG and PNG. CNS 11643 not tried (no sample could be written). Text files in the picker and the strip are plain tiles with their names, no preview of their words. WebP has no DPI.

## 2026-10-17: D-158 Blue for what can be pressed; the recent files in a list window

- Status: Accepted (owner 2026-10-17: "설정에서 누를 수 있는 기능은 파란색 글씨와 밑줄로"; "recent files 가 너무 보기 안좋다... 목록창 띄워서... 전체삭제나 삭제(목록 상에서만)... 날짜나 용량, 위치" — the date is when it was opened).
- Decision:
  - Settings: an action line's `< ... >`, a button's `[ ... ]` and a path setting's `[...]` are drawn in `SETTINGS_LINK` with their words underlined (`settings_text_link`); the accent that headings used was blue, so headings are amber now (`SETTINGS_ACCENT`).
  - Recent: `open_recent_list` (now `Ctrl+Shift+R`) opens `recentwin`, a `rubraview_listwin_t` with its bar's Del and Save places as Remove and Clear all; each row its box, its X, the name, the time it was last opened, its size (read when the window opens; "gone" for a file no longer there, dimmed) and its folder. A removal is kept in `recent.ini` as `[removed-N]` (path, time): an entry no newer than its removal is dropped wherever lists are merged, so another viewer's copy does not bring it back; opening the file again makes it new; clearing forgets the removals.
- Consequences: seen on the VM — two ticked and removed, one by its X, the list as left after the viewer was started again, Clear all to nothing; the links under the settings' words. The open-file screen's Recent page is still there, as tiles.

## 2026-10-18: D-159 Text files' tiles; the toolbox's words in English

- Status: Accepted (owner 2026-10-18: "남은 것 진행"; "툴바 아이콘 위의 텍스트는 언제나 영어로 ... 툴팁이나 텍스트 메시지 등은 언어에 따라 맞춰서 하지만 아이콘 안의 텍스트만 영어로 해야 넘치지 않아요").
- Decision:
  - RV-135: the thumbnail worker makes a text file's tile itself (`text_page_small`): its first 4 KB, the encoding guessed as the viewer guesses it, wrapped by `rubraview_pal_text_mask_box` and drawn dark on light paper in the tile's own shape, kept sharp (no shrink to 96 px, no blur).
  - `toolbox_caption` runs with no language pack (`rubraview_lang_use(NULL)`, put back after): a tile's words, including the ones made for its state (Play / Pause, Wide alone, the fit's name), are English; the tooltip (`hint_with_keys`), the menus and the messages are translated as before.
  - Text (owner, 2026-10-18): `text_autoscroll` on `A, Space` (`text_screen_down` keeps `PageDown`); a click with no drag in the middle third of the window is auto-scroll (`text_release`), a drag a selection that stops it; the wheel moves the text while auto-scroll runs; `toggle_playlist` on the text toolbox; `,` `.` stay the general previous / next file.
- Consequences: seen on the VM (2026-10-18) — text tiles in the picker and the strip; the toolbox in English on Korean Windows with its tooltip in Korean; and, left from 1.6.0, the settings tooltip, the font dialog, and the Store's menu with 400 files whose paths are 36,800 characters: one command line of 210 characters with `--paths-temp --paths-from`, the viewer on "1/400", no list file left in TEMP. A Johab or CP437 file's tile is read as the guess reads it (the viewer's E or the setting chooses another).
