# T069 — Keys are changed in the settings window

Covers: R148 (§3.22.2 tab 8, §3.7.5), D-14

## Preparation

No `settings.ini` or `keymap.ini` anywhere — AppData or beside the program —
and a video open in the viewer.

## Steps

1. `F10`, `Tab` seven times (the Keys page), `Down` seven times to
   **toggle_osd**.
2. `Enter`, then `F7`.
3. `Enter`, then `O`.
4. `Esc`. Read `%APPDATA%\rubraview\keymap.ini` with a TOML parser and an INI
   parser.
5. Start the viewer again and press `F7` three times.

## Expected

- Step 1: every binding is listed, and none is marked `! also …`. (Measured
  on 2026-09-14, before F2 went to rename alone, `toggle_toolbox` and
  `rename_file` were both marked — the clash RFC-0001 had written in.)
- Step 2: the row reads `> press a key (Esc: leave it)` after `Enter`, then
  `I, F7`; the message says `F7 added to toggle_osd`.
- Step 3: the row stays `I, F7`; the message says
  `not changed: O is open_picker's key`.
- Step 4: the file exists, starts with `[ui]` (no byte-order mark), has
  `toggle_osd = "I, F7"`, and both parsers read the same values.
- Step 5: the information bar at the bottom goes on, off, on.

## Measured on the Windows 11 VM, 2026-09-14 (v0.0.2, before 0.0.3)

All five. The file had 72 lines in 6 sections, identical under `tomllib` and
`configparser`. After the restart the bar's bright text pixels read 363, 0,
362 for the three presses.

Found while measuring, and fixed in the same change: F6–F9 had no Windows key
name, so `F7` pressed on a waiting row arrived as nothing and the row kept
waiting. An `Enter` sent next was then taken as the key — and refused,
because `Enter` is `next_page`'s, which is the refusal working as designed.

Not measured: a `keymap.ini` left in the working folder being read when
AppData has none (the code path is two lines in `load_keymap`).

## F2 given to rename, 2026-09-15 (v0.0.3 + D-14 amendment)

With no `keymap.ini`, `F2` on `c_plain.png` opened the rename box holding
`c_plain` (the extension kept aside); `Esc` left the file as it was. Until
this change the same key opened the toolbox.

