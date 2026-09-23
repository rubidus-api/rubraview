# T081 — Blu-ray picture subtitles (PGS, `.sup`)

Covers: SPEC §17, D-27

A Blu-ray captions the way a DVD does — a picture, not text — but in a
format that shares nothing with VobSub: one `.sup` file of segments, up to
256 colours, and the colours arrive as Y'CbCr. `src/core/pgs.c` reads it
and the viewer draws the picture over the film in the frame the disc
authored it for. `test_pgs` covers the reading; this case is about what
reaches the screen.

## Making a file to test with

There is no fixture in the repository (a disc rip is neither small nor
ours), so one is made: `python3 tools/mkpgs.py movie.sup` writes three
subtitles at 1 s, 4 s and 7 s, two seconds each, a 600x80 bar at 660,900
of a 1920x1080 frame, in white, yellow and pale blue. Name it after the
film (`clip_h264.sup`) and put it beside it.

## Steps and expected

1. Open the film. A bar appears low on the picture from 1 s to 3 s, again
   at 4 s and 7 s, each in its own colour, and nothing after 9 s.
2. The bar sits in the same place on the picture whatever the window's
   size or the zoom — it is drawn in the film's frame, not the window's.
3. `C` lists the `.sup` among the subtitle tracks (its kind reads `sup`)
   and turns it off and on again.
4. `Z` and `X` move the pictures in time as they move text subtitles.
5. A real disc rip: the subtitles appear where the disc put them, and a
   fade (a display set that only changes the palette) does not make the
   subtitle blink out.

## Known limits, written down so they are not rediscovered

- A display set that composes two objects (dialogue above and below the
  frame at once) is drawn as its first object. Both at once is backlog.
- A later composition inside one epoch that refers to a picture sent by an
  earlier set, without sending it again, is not drawn: each subtitle is
  decoded from its own display set. Rip tools normally send one epoch per
  subtitle, so this is rare.

## Measured on the host, 2026-09-24

`tools/mkpgs.py` and the reader agree end to end: three subtitles,
1.00–3.00, 4.00–6.00, 7.00–9.00, each 600x80 at 660,900 of a 1920x1080
frame, every one of the 48000 pixels drawn, colour 1 reading `FFFFFFFF`
(white), `FFFFF000` (yellow) and `FFACC1FF` (pale blue) — the Y'CbCr the
generator wrote, converted.

## Measured on the Windows 11 VM, 2026-09-24

The build run from its own folder (`rbpgs`), holding only the exe, the
film `clip_h264.mkv` and `clip_h264.sup` from `tools/mkpgs.py`. Read from
screenshots, the pixel in the middle of the bar:

- 2.60 s: a bar low on the picture, `(255, 255, 255)` — the first
  subtitle, white, exactly the colour the reader computes from its
  Y'CbCr;
- 4.94 s: `(255, 240, 0)`, the second — yellow;
- 9.98 s: nothing; the last subtitle ended at 9 s.
- `C`: the bar goes and the film is its own again; `C` again brings it
  back (and the track list holds the `.sup`).
- `Z` / `X`: five `X` (+2.5 s) at 4.94 s shows the *first* subtitle,
  white; ten `Z` from there (−2.5 s) shows the *third*,
  `(172, 193, 255)` — the pale blue, and the only way to reach it by
  keyboard, since the arrows seek five seconds at a time.
- Step 2: `F` took the window from 1280x752 to 1280x800. The bar's
  bounding box was the same size and the same x, and its y moved by 24 —
  exactly the letterboxing the taller window adds. It is drawn in the
  film's frame, not the window's.

Found by this case (2026-09-24): `Z` and `X` refused to move a picture
subtitle at all — the guard asked whether the *text* track had cues, and
a DVD's or a Blu-ray's pictures are not text. It said "no subtitles are
showing" while a subtitle was plainly on screen. Fixed, and the remembered
sync now comes back for a picture track as well; both are in the same
commit as this case.
