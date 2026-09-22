# Changelog

All notable changes to this project will be documented in this file.

This project follows Keep a Changelog.

## [Unreleased]

### Fixed

- A photo stored sideways (an EXIF orientation such as a phone's portrait
  shot) took seconds to open — 6.5 s for 1600x1200, 90 s for 12 megapixels
  on the test VM: turning it re-decoded the whole JPEG for every row. It is
  now decoded once and turned in memory (22 ms and 0.2 s). The editor's
  pixel path had the same fault.

## [0.0.9] - 2026-09-22

### Changed

- The toolbox is a strip: a seek bar on top, the file's name under it,
  then small icon buttons in rows — previous and next file, −5 s, +5 s,
  play/pause, stop, volume, and so on. Pointing at a button shows what it
  does in place of the name. The detached toolbox window shows the same.
- Both floating boxes have a pin at their top-left: pinned, a box stays
  open when the pointer leaves. Clicking an anchor's left half opens the
  box pinned; dragging it moves the box; the right half opens on hover.

### Added

- Settings › Video explains how to add FFmpeg: which DLLs (not the exe),
  where they go, which version (8.1) and where to download it.

## [0.0.8] - 2026-09-22

### Fixed

- With GPU decoding on, a real graphics card (the owner's Intel UHD 730)
  showed a black window — seek bar and boxes too — and the film timed out
  opening: the decoder was lent the very device Direct2D draws with, and
  Media Foundation's threads used it alongside. The decoder now has a
  device of its own on the same card, and each frame crosses to the
  renderer as a shared texture under a keyed mutex — still on the card.
  `--probe-gpu` gains a last step that runs this whole path through the
  real renderer (a hidden window) and reads back what was drawn.

### Changed

- The floating boxes' right-hand anchor button only hovers; dragging and
  clicking are the left one's.

## [0.0.7] - 2026-09-22

### Fixed

- On a PC where the viewer had never run, a change in the settings window
  ended in "could not write settings.ini" and was lost — and the reading
  history, box positions and keys were not kept either: the folder they
  live in (`%APPDATA%\rubraview`) did not exist yet, and nothing made it.
  Writing a file now makes its folder first.
- Files are read and written by their UTF-16 path, so a Windows user name
  in Hangul (which puts Hangul in `%APPDATA%`) no longer breaks them.

## [0.0.6] - 2026-09-22

### Changed

- `--probe-gpu` (and so `gpu-check.cmd`) tries `hardware_decode = off` as
  well as `on` and `always`, says why a film did not open (HEVC without
  the extension, no decoder, …), and for each one decodes flat out for
  four seconds and reports pictures per second and the CPU it cost — one
  run compares software and the card.

## [0.0.5] - 2026-09-22

### Added

- `gpu-check.cmd` in the release folder: drag a video onto it and it writes
  `gpu-check.txt` — what this PC does with decoding on the graphics card
  (T065), to send back.
- Video can be decoded on the graphics card (RV-062), behind the Video
  page's **GPU decoding** setting: `off` (the default until it has been
  measured on a real card), `on` where the card offers decoders, `always`
  for diagnosis. A decoded frame stays on the card and is copied into the
  picture there — no trip through system memory (on the test VM that alone
  took 30 % off the CPU a film uses). A device the decoder cannot use is
  not handed over, and one that fails is dropped: the film plays in
  software as before. `--probe-gpu` says what this machine will do with it.
- The batch dialog's **Run on this folder** runs (RV-068). It writes the
  command line `--batch` already understands and starts a second copy of
  the program with it, in a console window of its own, over the folder
  being read and into a `rubraview-out` folder beside those pictures — so
  a run can never overwrite an original, keeps going when the viewer is
  closed, and survives a viewer that hangs (owner, 2026-09-20). The window
  stays open at the end (`--pause`) so the counts can be read.
- The crop rectangle can be dragged on the picture (§3.13): what is cut
  away is dimmed, the corners carry grips, and the size is written above
  it. The model — normalising, the ratio lock, staying inside the image —
  was already there; this is the overlay that drives it.
- The curve widget is drawn, under the workbench panel: the active
  channel's curve over a quarters grid, its control points as handles.
  Drag one to move it, right-click an interior one to take it off.
- Several files dropped together open as a set of exactly those files,
  in natural name order, instead of only the first one (§3.19.2). They
  may come from different folders.

- Keys can be exported to a file and imported from one, on the settings
  window's Keys page (§3.22.2). The file dialogs now show their title and
  file type, and a name saved without an extension gets `.ini`.

### Changed

- The menu and the toolbox say more before they are tapped: a tile that
  opens a submenu ends in `>`, a switch says whether it is on
  (`Crisp: off`, `Spreads: on`), the layout and the fit in use have a blue
  edge, and whatever cannot do anything with what is on screen is grey and
  ignores a tap — in the menu as the toolbox already did (`Next archive` in
  a plain folder, `Frame >` on music, `Track` with one sound track).
- (D-18, owner) A menu level holds up to 16 tiles instead of 12. The
  menu's `Delete` asks first: one tap turns it into `Delete?`, a second tap
  within five seconds acts. The reading-order tile says `Order: L>R` or
  `Order: R>L`. Open folder lists folders and the files the viewer opens,
  and says how many others it left out.
- Names that meant two things: a comic's next or previous *archive* is
  called that (the menu said "volume", the toolbox "Vol <" beside the sound's
  "Vol -"); the sound-track tile is `Track` and the mute tile says `Unmute`
  while muted (a film's toolbox showed two tiles called `Sound`); the menu's
  `Full` is `Fullscreen`.

### Fixed

- Clicks on a seek: the new position started mid-wave and the old one was
  cut mid-wave. The sound now fades in over 5 ms after a seek, and the
  stream is stopped and given 30 ms to end quietly before its queue is
  thrown away (T058, measured on 12 seeks: no click left).
- The picture waits for the sound as it is played, not as it is handed to
  Windows: the audio clock follows the device's own position
  (`IAudioClock`), which includes the engine's and the device's latency —
  a Bluetooth headset's 150-250 ms among them.
- Short drop-outs (10 ms of silence, a click) when the machine was busy:
  the thread that feeds the sound device now runs under Windows'
  multimedia scheduler (MMCSS "Playback").
- A sound device that went away during playback (unplugged, disabled, a
  remote session's sound turned off) froze the film or music where it
  was. It now plays on silently at the right speed, seeks still work, and
  when a device comes back the sound carries on there.
- The menu reopened on the level it was closed on, so a tap meant for the
  top level landed on whatever sat in that place one level down (`Delete`
  where the top level has `Show`). It opens at its top level again, however
  it is opened.
- A playing sound kept the viewer redrawing its window on every pass of
  its loop. Without a GPU (Direct2D then draws on the CPU) that was most
  of two cores for a picture that did not change, and on the test VM it
  slowed everything else down: key presses sent by the test tools took
  half a minute to arrive. While only the film or the sound moves, the
  window is now redrawn for each new picture and otherwise four times a
  second for the seek bar; a sound-only page used a fifth of the CPU it
  did (measured on the VM, T058).
- T058 named the seek keys from before D-16 (`Ctrl+Right` / `Ctrl+Left`,
  which now size the window); it says `Right` / `Left` now.

- With a Korean IME in Hangul mode, letter shortcuts did nothing: the IME
  took the keys and the window never saw them. The viewer's window is no
  longer attached to the IME, so `F`, `T` and the rest work in either mode
  (RFC-0003 K5).
- A batch run that converts the format wrote the new bytes under the old
  extension — a PNG inside a `.jpg`. The output is named after the format
  it actually holds. This was true of `--batch --format=` too.

- Dragging the toolbox's anchor past the window's edge now detaches it, as
  §3.6.1 says: the window did not keep the mouse while a button was held,
  so the drag stopped at the edge.
- The Keys table kept its old height after Defaults or Revert changed how
  many keys there were.
- A floating box's anchor left near the edge of a window that then shrank
  was out of reach outside it; anchors are pulled back in on every resize.

## [0.0.4] - 2026-09-16

Toolbox and menu that change with what is on screen (D-15), keys from the
keymap alone — arrows play, Ctrl+arrows size the window, Alt+arrows move it
(D-16) — and Always on top (D-17).

Going back to 0.0.3 after running this version: a `keymap.ini` this version
saves names its playback section `[media]`, which 0.0.3 does not read, so
the keys in it are lost there. Delete `keymap.ini` first.

### Added

- Volume and mute (D-15): `Up` / `Down` change the volume 5 % at a time
  while a video or music page is on screen, `Shift+M` mutes; the OSD says
  the level, and `settings.ini` keeps it (`[audio] volume`, `mute`).
- The floating boxes' anchors read `M` (menu) and `T` (toolbox), and `Alt`
  + wheel over a box makes it more or less see-through, 5 % a step, never
  below 30 %; each box keeps its own (`[ui] menubox_opacity`,
  `toolbox_opacity`). A lone `Alt` no longer puts the window in menu mode.
- The toolbox changes with what is on screen (RFC-0002): a video gets
  Play/Pause, Stop, ±5 s, volume, mute, subtitles and sound track; music its
  own row with previous and next; an animated picture frame steps; a
  multi-page TIFF page steps; a comic archive layout, reading order and
  volumes; a picture today's tiles. A running slide show adds Stop slides.
- The menu box has File (Open file, Open folder, Recent, volumes, Rename,
  Delete, Export, Batch, Settings, Quit), View, Playback (while a video or
  music page is on screen), Show (with the boxes' opacity) and Help (Keys,
  About). Tiles and menu come from one internal document, checked by
  `check-actions.py`.
- A timeline above the information bar while a video or music plays:
  elapsed, a seek bar to click or drag, total, volume and speed.
- Playback speed 0.25x–4x (`Ctrl+]` / `Ctrl+[`, `Ctrl+\` for normal, the
  toolbox's speed tile cycles the usual ones); the sound is resampled, so
  its pitch follows. A-B repeat: `[` sets A, `]` sets B, `\` turns it off,
  or the toolbox's A-B tile.
- The toolbox can be pinned open (`Shift+T`, Show › Pin toolbox; its
  anchor shows `*`) and detached into a small window of its own that stays
  on top (`Ctrl+T`, Show › Detach toolbox, or drag its anchor past the edge);
  `Dock` brings it back.
- Always on top (owner request): a `Pin` button on the hover titlebar (lit
  while on), the menu's first tile `On top: on/off`, `Ctrl+Shift+T`, and
  **Always on top** on the settings window's General page; it is kept.
- The hover titlebar draws its `Box` button (put the floating boxes back),
  which was there to click but never drawn.
- The window from the keyboard (D-16): `Ctrl` + arrows make it narrower,
  wider, shorter or taller, `Alt` + arrows move it, 40 px a step.
- `scripts/check-actions.py`: every key, tile and menu item is handled, and
  everything handled is reachable.

### Fixed

- `T` or the titlebar's box button while the toolbox was its own window
  drew a second toolbox inside the viewer; both now put the window back.
- The reading history's `time` was seconds since the machine started, so
  after a restart "newest" meant nothing; it is the date now. Entries
  written before sort as the oldest.

### Changed

- While a video or music page is on screen, `Left` / `Right` seek 5 s
  (they were `Ctrl+Left` / `Ctrl+Right`, which now size the window). The
  playback context is `[media]`; a `keymap.ini` that says `[animation]`
  still loads. Up to the folder is `Ctrl+Backspace` (was `Ctrl+Up`).
- An open box sits beside its anchor and inside the window; by the
  bottom-right corner the toolbox used to open over its own anchor and off
  the screen.
- The four `Alt` + arrow "pan" bindings, which did nothing, are gone.
- `F2` renames the file, as in Explorer; it no longer also opened the toolbox,
  which is `T` (D-14). Until now `F2` reached the toolbox and rename could not
  be started from the keyboard. A `keymap.ini` saved earlier keeps `T, F2` for
  the toolbox — the Keys page marks it; `Delete` on that row takes F2 off.

## [0.0.3] - 2026-09-14

Video and audio (M5), the settings window as a window of its own, and keys
changed in it.

Going back to 0.0.2 after running this version: 0.0.2 does not take the
quotes off text values, so the files this version writes read wrong there —
`decoder = "ffmpeg"` falls back to Media Foundation and quoted folder paths
do not resolve. Delete `settings.ini`, `history.ini` and `keymap.ini` first.

### Added

- Video and audio playback (M5) is complete except the sound check that needs
  a machine with speakers (T058). Media Foundation plays the file by default
  and FFmpeg takes the ones it cannot (D-8, D-9); a file nothing can open is
  reported and skipped. Subtitle files beside a video are found, drawn with an
  outline and nudged half a second at a time; the sound tracks of a file and
  the subtitle files beside it can both be switched while it plays (D-10). A
  slide show holding a film waits for the whole film. Hardware decode is not in
  this milestone and will be the zero-copy path when it is built (D-11).
- settings.ini is read when the viewer starts, not only when the settings
  window is opened — until now every setting read while viewing came back 0.
- Subtitles carried inside a file are shown too, not only files beside it:
  the FFmpeg backend lists a film's text subtitle streams and reads one by
  walking the container again, so no subtitle decoder is involved (D-12).
  Picture-based subtitle formats are not offered — nothing can draw them.
- The floating boxes come back where they were left (layout.ini).
- The settings window is a window of its own (F10). Its pages are laid out
  from one internal document that also declares every setting's type, range
  and default; a change applies to the viewer at once, the file is written when
  the window closes, and Revert goes back to what it held (D-13). The Keys page
  lists every binding, scrolled to the last; the Video page previews the
  subtitle at the chosen size and outline. The window comes back where it was
  left, at the size it was left (layout.ini), pulled onto a screen if that
  place is gone. A decoder chosen there is used at the next launch too.
- Keys are changed on the settings window's Keys page: `Enter` adds the key
  you press to an action, `Delete` takes its last key off, Revert and Defaults
  undo. A key another action already answers to is refused and that action
  is named; rows whose keys clash are marked. `keymap.ini` is written next to
  `settings.ini` when the window closes (D-14). F6–F9, `;`, `/`, `'`, `` ` ``
  and the number pad now have key names.
- Configuration files are written in the common subset of INI and TOML: quoted
  strings, `true`/`false`, plain numbers, sections for everything. Older files
  still load and are rewritten on the next save. The reading history now keeps
  one `[entry-N]` section per book; the General settings live under
  `[general]`; the built-in keymap's top actions under `[ui]`.

## [0.0.2] - 2026-09-11

Found by running the viewer on a Windows 11 test machine.

### Added

- The window title names the file on screen and its place in the folder, e.g.
  `c_plain.png (3/5) - Rubraview 0.0.2`.

### Fixed

- An idle viewer no longer redraws an unchanged picture. On a machine without a
  GPU it kept two CPU cores busy; it now sleeps until there is input.
- Pictures being viewed, and the neighbours read ahead of them, are no longer
  locked: they can be renamed, deleted or saved over while the viewer is open.
- Pages read ahead are decoded on the main thread; they were decoded on worker
  threads that the imaging and drawing code do not support.
- The first window fits inside the screen's work area instead of covering the
  taskbar on a small screen.

## [0.0.1]

### Added

- Comic archives: `.cbz`/`.zip` and `.cb7`/`.7z`, read straight from memory with
  nothing unpacked to disk. Reaching the last page of one volume opens the next.
- Reading history with a resume prompt, and portable mode — a `settings.ini`
  beside the executable keeps everything off the host machine.
- A pre-cache ring so page flips do not wait for the disk, bounded by a memory cap.
- Animated GIF, WebP and APNG playback with pause, frame stepping and a speed
  ladder; multi-page TIFF and multi-size ICO as steppable sub-pages.
- Wide-gamut colour management through the Windows Imaging Component.
- An editing workbench (`E`), quick export (`Ctrl+E`) and a batch mode
  (`rubraview.exe --batch ...`) that runs without a window.
- Lossless JPEG rotation and metadata stripping: turning or cleaning a JPEG
  never decodes it, so no quality is lost.
- File triage: recycle-bin delete, permanent delete with a confirmation, rename,
  and `1`-`9` quick-folder sorting, all with undo where undo is possible.
- Single-instance window reuse, drag-and-drop, and `--register-shell` /
  `--unregister-shell` file associations under `HKCU`.
- A settings window (`F10`) with eight tabs, writing `settings.ini`.
- `make package`: the release layout under `dist/rubraview-v<version>/`, with the manual and
  the vendored libraries' licences, and a record of which DLLs the executable
  imports.

### Changed

- The renderer moved from Direct2D 1.0 to a Direct2D 1.1 device context. Cubic
  interpolation is now real rather than silently falling back to linear, and the
  slide show cross-fades.

### Security

- Archive reading enforces a size cap before allocating: for a 7z the cap is on
  the solid block, which is what actually gets allocated. Encrypted archives are
  refused rather than half-handled.

### Changed

### Deprecated

### Removed

### Fixed

### Security
