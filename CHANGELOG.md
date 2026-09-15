# Changelog

All notable changes to this project will be documented in this file.

This project follows Keep a Changelog.

## [Unreleased]

### Added

- Volume and mute (D-15): `Up` / `Down` change the volume 5 % at a time
  while a video or music page is on screen, `Shift+M` mutes; the OSD says
  the level, and `settings.ini` keeps it (`[audio] volume`, `mute`).
- The floating boxes' anchors read `M` (menu) and `T` (toolbox), and `Alt`
  + wheel over a box makes it more or less see-through, 5 % a step, never
  below 30 %; each box keeps its own (`[ui] menubox_opacity`,
  `toolbox_opacity`). A lone `Alt` no longer puts the window in menu mode.
- The window from the keyboard (D-16): `Ctrl` + arrows make it narrower,
  wider, shorter or taller, `Alt` + arrows move it, 40 px a step.
- `scripts/check-actions.py`: every key, tile and menu item is handled, and
  everything handled is reachable.

### Changed

- While a video or music page is on screen, `Left` / `Right` seek 5 s
  (they were `Ctrl+Left` / `Ctrl+Right`, which now size the window). The
  playback context is `[media]`; a `keymap.ini` that says `[animation]`
  still loads. Up to the folder is `Ctrl+Backspace` (was `Ctrl+Up`).
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
