# T078 — DVD picture subtitles (VobSub)

Covers: SPEC §17, D-22

A DVD does not caption with text: each subtitle is a small picture of four
colours. `src/core/vobsub.c` reads the pair — `movie.idx` (palette, frame
size, languages, each subtitle's time and where its picture is) and
`movie.sub` (an MPEG program stream of subpictures) — and the viewer draws
the picture over the film. `test_vobsub` covers the reading; this case is
about what reaches the screen.

## Making a pair to test with

There is no fixture in the repository (a disc rip is neither small nor
ours), so one is made: `python3 tools/mkvobsub.py STEM` writes `STEM.idx`
and `STEM.sub`: three subtitles, at 1 s, 3 s and 5 s, two seconds each, a
400x60 bar at 160,380 of a 720x480 frame, in three palette colours. Name
them after the film (`clip_h264.idx`, `clip_h264.sub`) and put them beside
it.

## Steps and expected

1. Open the film. A bar appears at the bottom from 1 s to 3 s, again at
   3 s and 5 s, each in its own colour, and nothing between 7 s and the
   end.
2. The bar sits in the same place on the picture whatever the window's
   size or the zoom — it is drawn in the film's frame, not the window's.
3. `C` lists the `.idx` among the subtitle tracks (its kind reads `idx`)
   and turns it off and on again.
4. `Z` and `X` move the pictures in time as they move text subtitles.
5. A real disc rip with several languages in one `.idx`: the first
   language is shown (choosing between them is backlog, D-22).

## Measured on the Windows 11 VM, 2026-09-23

The pair made by `tools/mkvobsub.py`, named `clip_h264.idx` / `.sub` and
put beside `clip_h264.mkv` (640x360, ten seconds). By screenshot, reading
the pixel in the middle of the bar:

- 1.479 s: a bar low on the picture, `(255, 64, 64)` — the first subtitle's
  colour, palette entry `ff4040`, exactly;
- 4.938 s: the second subtitle, white;
- 8.604 s: nothing — the last one ended at 7 s;
- `C`: the bar goes (the pixel is the film's own again); `C` again brings
  it back, and 1.479 s is red once more.

Step 2 (the same place whatever the window) and step 5 (a real rip with
several languages) are not measured: the test viewer fills the screen, and
there is no rip on this machine.
