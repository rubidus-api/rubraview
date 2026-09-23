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

## Measured on the Windows 11 VM

NOT RUN — the VM is lent out; to be run with the packaged build from its
own folder (the trap T078 found).
