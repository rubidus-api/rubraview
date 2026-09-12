# T063 — settings.ini applies from the first frame

Covers: R146 (§3.22.1), R135

Every setting the viewer reads while viewing must come from `settings.ini`
at startup. Until 2026-09-13 the file was read only when the settings
window was opened, so a reader who had never pressed `F10` in that run got
0 for every setting — including the subtitle size and outline.

The two subtitle settings are the witnesses, because their effect is on the
screen and needs no sound.

## Preparation

A video with a subtitle file beside it (see T061), and a `settings.ini`
containing:

```ini
[video]
subtitle_size = 48
subtitle_outline = 6
```

Portable mode puts it beside the executable; otherwise it goes in
`%APPDATA%\rubraview\`. Write it **without a BOM** — a byte-order mark ahead
of `[video]` hides the whole section.

## Steps

1. Open the video. Do not open the settings window.
2. Look at a subtitle over a bright part of the picture.
3. Change the file to `subtitle_size = 24`, `subtitle_outline = 0`, start again.

## Expected

- Step 2: the text is about twice the default height and carries a thick
  black outline.
- Step 3: the text is back to its ordinary size with no outline at all.
- Neither needs the settings window to have been opened.

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

Both, in screenshots. With the fix the 48/6 file gives a large outlined
subtitle on the first frame it appears; before the fix the same file changed
nothing (the drawing used 0 for both, so the size fell back to its own
default and no outline was drawn).
