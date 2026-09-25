# Changelog

All notable changes to this project will be documented in this file.

This project follows Keep a Changelog.

## [Unreleased]

### Added

- The rename box edits like a text field: the arrows, Home and End move
  the caret (Shift selects), Ctrl+A selects everything, typing replaces
  the selection, and Ctrl+C, Ctrl+X and Ctrl+V use the clipboard — Hangul
  included. The same box types the picker's "Change ext".

- Drop pictures from other programs (RV-073, D-36). The window takes drops
  the way Explorer offers them and also the way browsers and mail programs
  do — a picture with no file behind it is copied to a temporary folder of
  the viewer's, opened, and removed when the viewer closes.

- Thumbnails in the file picker (owner, 2026-09-25, D-34, D-35). Each tile
  shows its picture, softly blurred and a little dark so the name — light
  with a dark outline — reads over it; a folder shows its first picture, or
  its first film; a comic archive its first page. The list shows at once
  and the pictures are made in the background, added as each is ready, so
  the picker answers input throughout. An archive whose first page would
  take too long to read (over about 1.5 s at the speed the disk is giving,
  or a very large page) keeps a plain tile.
- Subtitles in a box of their own (owner, 2026-09-25, D-33). A tap on the
  subtitle selects its translucent box; S, M, R and X appear at its corner:
  drag M to move it, drag R to resize it (which sets the subtitle size), S
  opens the subtitle settings, X turns subtitles off. The box stays where
  it was left. The toolbox's Sub button turns subtitles off and on; a
  double tap or a held press opens the list of subtitle tracks to choose
  from. Settings › Video gains the box's shade.

- The music in a window of its own (owner, 2026-09-24, D-31, RV-081).
  `Shift+P` opens a small player on top of whatever is being read — the
  cover, the track, who made it, a strip to seek on and three buttons —
  driving the background music when there is any and the page's own track
  otherwise.
- Music plays on while pictures are looked at or a comic is read, and
  stands aside for a film with sound of its own (owner, 2026-09-24,
  D-30, RV-081). The listener's own pause outranks that: music stopped by
  hand stays stopped. `Space` with no player on the page is the
  background music's.
- One track runs into the next (owner, 2026-09-24, D-29, RV-075). The
  next file in the folder is opened while the current one still plays and
  takes over at its end with no gap; with `[audio] crossfade_seconds` set
  the two overlap on an equal-power curve instead. Both settings are live
  on the Audio page.
- The A-B repeat points can be typed as well as tapped (owner,
  2026-09-24, D-28). `Shift+\` opens a box holding both as numbers;
  inside it `[` and `]` put the playhead into the field in hand, and the
  keyboard types over it — so a point tapped in can be corrected to the
  millisecond and a typed one can be re-tapped.
- The music page shows the record (owner, 2026-09-24, RV-076): the cover
  in the middle, the same cover blurred behind it, and what the file says
  about the track — title, artist, record, year, track number, codec,
  bitrate, sample rate and channels — along the bottom. All of it is read
  from the file's own bytes (`src/core/tags.c`: ID3v2.2/2.3/2.4, ID3v1,
  FLAC, MP4/M4A, Ogg Vorbis, Opus, WAV), so it does not depend on which
  backend is playing.
- Blu-ray picture subtitles (owner, 2026-09-24). A `.sup` file beside the
  film is offered as a subtitle track and drawn over the picture in the
  frame the disc authored it for, the way a DVD's `.idx`/`.sub` pair is
  (D-27). Reading it needs neither FFmpeg nor a decoder.

### Changed

- Files that share a modified time, a creation time or a size are ordered
  by name, then by the listing, so a folder opens the same way every time;
  before, their order was whatever the filesystem listed. Sorting is
  proven's introsort now, never quadratic.
- Bicubic and Lanczos-3 resizing in export and batch runs 1.4-2.4x faster,
  with the same output byte for byte.
- Settings, playlists, subtitles and tags read their numbers with
  proven's parsers: a value that is not a finite number (`nan`, `inf`, too
  many digits) falls back to the default instead of being used.

### Fixed

- The rename box, the A-B box, the delete question and the notices are
  drawn above the floating boxes; the toolbox's anchor covered the start of
  a name being typed.
- The subtitle box's default place is just above the seek bar, which used
  to cover its last line.
- The picker's path bar stays on top when the tiles are scrolled; rows no
  longer show through it.
- A folder's thumbnail skips sound files and tries the next picture or
  film when the shell has nothing for the first.
- Settings › Video said FFmpeg must be 8.x when it was not found; it is 9.x
  since 0.0.17.
- A comic opened from Explorer runs into its next volume. Past the last
  page nothing happened when the archive's path came with backslashes, as
  Explorer and the command line give it — the viewer compared whole paths
  byte for byte against the folder listing, which joins with `/`. Stepping
  back past the first page had the same fault.
- The place to resume a volume is found however its path is spelt: a
  volume reached by paging on and the same file double-clicked later are
  one book, not two.
- A long whole number in an INI file no longer overflows while it is
  read (`rubraview_ini_get_int`), and a picture whose claimed width times
  its pixel size does not fit is refused instead of overflowing.
- The window title names an archive page (`2.png (2/4) - Rubraview`); it
  showed only ` (2/4)`.
- A file whose sound cannot be played now says so — which call failed and
  what it returned — instead of looking exactly like a file with no sound
  in it.
- `Z` and `X` move a DVD's or a Blu-ray's picture subtitles too. They
  moved only text before, and said "no subtitles are showing" while a
  subtitle was plainly on screen (found measuring T081).

## [0.0.17] - 2026-09-24

### Changed

- FFmpeg 9.0 is the version to put beside the program (owner, 2026-09-24).
  The headers here are 9.0.2's, so the DLLs are `avcodec-63`,
  `avformat-63`, `avutil-61`, `swscale-10` and `swresample-7`. FFmpeg 8.1's
  DLLs are refused as any other version is — the viewer says so and uses
  Windows' own decoders, as it does when FFmpeg is absent.

### Fixed

- A subtitle file is read whatever order it is written in (owner,
  2026-09-23): the captions are put in time order before anything else is
  worked out, so a SAMI whose languages do not take turns, or a SubRip
  numbered out of sequence, reads the same as a tidy one.
- A SAMI caption now ends where its own language says `&nbsp;` — it used
  to hang on until the next caption, showing text the file had cleared.
- The last caption of a SAMI file is kept. Nothing closed it before, so a
  file that does not end with a blank lost its final line.

## [0.0.16] - 2026-09-23

### Fixed

- A subtitle file holding several languages at once — a SAMI `.smi` with
  a class per language, which is how Korean subtitles usually come — now
  shows. Every caption used to end where the next one of *any* language
  began, so each was of zero length and none of them ever appeared. Each
  language is now its own track: `C` walks them, named by the class.

### Changed

- GPU decoding ships **on** (owner, 2026-09-23), now that two real cards
  have been measured: at 4K, 196 pictures a second on an AMD Radeon and 43
  on an Intel UHD 730, against 16-21 in software, with a tenth of the
  processor. `off` and `always` are still there, and the settings window
  now says what each of the three means — `always` is for testing.

### Added

- Both READMEs say where FFmpeg's DLLs come from, which five they are, and
  that the `lib` folder's `.dll.a` files are not them.

### Added

- A DVD index that holds several languages now gives one subtitle track
  per language, named by the `id:` in the index, so `C` walks them like
  any other subtitles. Before, only the first language could be shown.

## [0.0.15] - 2026-09-23

### Added

- `F1` opens the help: a window of its own, which stays open while the
  viewer is used. It lists every key that is bound, grouped by where it
  works, named the way the menu names it — and it is read from the keymap
  in force, so a rebound key shows its own binding. The wheel, `PageUp`
  and `PageDown` scroll it; `F1` or `Esc` closes it.
- Both READMEs gain a **Keyboard shortcuts** section. The table is
  generated from the shipped keymap by `scripts/check-actions.py --write`,
  in English and in Korean, so it cannot drift from the program.

### Changed

- `F1` was a second key for the menu box; the menu keeps `Tab`.

## [0.0.14] - 2026-09-23

### Added

- Picking several files without holding a key (owner, 2026-09-23): the
  picker's bottom bar gains **Individual** (every tap turns one file on or
  off), **Range** (tap two, and everything between them turns over),
  **Same type** (every file with the focused file's extension), **Change
  ext** (the picked files all take one typed extension), **Playlist**,
  **Recycle**, **Move to**, **Copy to** and **Clear**. A mode stays on
  until it is turned off.
- **Playlist** writes the picked files as `PlaylistNNNN.m3u8` in the folder
  and opens them as one sequence. **Recycle** asks by wanting a second
  press. **Move to** and **Copy to** wait for a number key, which names one
  of the folders on the settings window's Files page. Every one of them can
  be undone with `Ctrl+Z`, one file at a time.
- The picker lists `..` first wherever there is a folder above.

### Changed

- The rename box (`F2`) holds the whole name, extension included, so an
  extension can be corrected. It is not asked about (owner).
- The rename box draws a caret, which blinks.
- The picker draws the messages and the text box itself, which it used to
  cover: pressing its buttons said nothing at all before.

### Fixed

- The number keys (1-9) send a file to the folders set on the settings
  window's Files page — they never did. Those folders, and portable mode,
  were read from `settings.ini` *by name alone*, so they followed whatever
  folder the viewer happened to be started in rather than the settings file
  it actually uses. `history.ini` and `layout.ini` had the same fault in
  portable mode.
- While the picker is open a number key answers its "Move to" or "Copy
  to" instead of sending the page behind it to a folder.

## [0.0.13] - 2026-09-23

### Added

- DVD picture subtitles: a `.idx` beside a `.sub` is listed with the other
  subtitle tracks, and its pictures are drawn over the film where the disc
  put them. The index is read whole; each picture is decoded only while it
  is on screen. `Z` / `X` move them in time like any other subtitles.

## [0.0.12] - 2026-09-23

### Fixed

- A film with several sound tracks starts on the one the file lists first.
  On a Korean Windows, Media Foundation numbered the tracks its own way and
  the viewer took the Korean track of a file whose first track is English;
  the FFmpeg backend picked by its own "best" rule. Both now go by the
  file, and the track list starts with that track.

## [0.0.11] - 2026-09-23

### Fixed

- Korean (and any IME) can be typed into the rename box (F2): the syllable
  being composed shows in the box, Enter keeps it. Capitals and symbols go
  in as typed — before, every letter was lowercased and only letters and
  digits went in.

## [0.0.10] - 2026-09-22

### Changed

- The pin no longer takes a row at the top of an open box: it appears as a
  third square beside the anchor (to its left when the window has no room
  on the right) while the box is open, and goes with it.

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
