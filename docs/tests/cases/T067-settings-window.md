# T067 — The settings window: its own window, changes apply at once

Covers: R148 (§3.22.1), D-13

`F10` opens a window of its own, owned by the viewer's. Every page is laid out
from the settings document on a grid of fixed-width cells. A change shows in
the viewer immediately; the file is written when the window closes; Revert
returns to what the file held when the window opened.

## Preparation

A video with a subtitle file beside it, no `settings.ini` anywhere (so the
defaults are what is on screen), and the viewer showing the video paused at a
moment a subtitle is up.

## Steps

1. Press `F10`.
2. `Tab` four times (General → Viewer → Files → Audio → Video), `Down` twice to
   **Subtitle size**, then `PageUp` twice.
3. Move to the buttons (`End`, `Down`) and press `Enter` on **Revert**.
4. Change **Subtitle outline** (`Home`, `Down` three times, `PageUp` twice),
   then press `Esc`.
5. Open `settings.ini` and read it with a TOML parser and an INI parser.

## Expected

- Step 1: a second top-level window titled **Rubraview settings**, above the
  viewer, with the page list on the left, the page title, `── Section ──`
  headings, `[x] on` toggles, `< word >  i/n` choices, `[####----] 24 pt`
  sliders, the `< Register file types >` actions, the settings file path, and
  `[ Revert ]  [ Defaults ]  [ Close ]` at the bottom. Settings nothing reads
  yet are grey.
- Step 2: the Video page shows the FFmpeg line filled from the running viewer
  and a subtitle preview block; **the subtitle on the video grows while the
  window is still open**, and the preview grows with it (drawn no taller than
  its block).
- Step 3: the size is back to 24 pt, in the window and on the video, with a
  line saying what Revert did.
- Step 4: the outline on the video thickens as it changes; `Esc` closes the
  settings window and **the viewer keeps running**.
- Step 5: no byte-order mark; both parsers read the file, and read the same
  values — `subtitle_size = 24`, `subtitle_outline = 8`.

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

All five, in screenshots and by reading the file the viewer wrote back through
`tomllib` and `configparser`. A second run with size 45 and outline 8 wrote
`45` and `8`, read the same by both.

Found while measuring, and fixed in the same change: an 8 px outline drawn as
four displaced copies came apart into ghost text (now eight directions in two
rings), and the preview's text spilled out of its block at large sizes.

> Sending keys to the settings window needs its handle, not the process's main
> window: `vmkeys.sh <keys> 1 50 <shot-ms> "Rubraview settings"`. And a window
> the settings window covers can be uncovered with
> `vmwinpos.sh "Rubraview settings" 640 0 640 520`.
