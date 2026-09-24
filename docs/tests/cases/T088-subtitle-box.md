# T088 — The subtitle box

Covers: RFC-0001 §3.16.1-§3.16.2, D-33

Text subtitles are drawn in a translucent box the reader can move and
resize. A tap selects it; a selected box has a thick edge and four buttons
above its top-right corner: S (settings), M (move), R (resize), X
(subtitles off). The toolbox's Sub tile turns subtitles off and on; a double
tap or a held press on it opens the list of tracks.

Host coverage: `tests/test_subbox.c` (geometry, clamping, hit tests, drags,
the tap/double/hold decision, the list's rows).

## Steps and expected

1. Open a film with two subtitle files beside it (`film.eng.srt`,
   `film.kor.srt`). The subtitle sits in a shaded box at the bottom, 90% of
   the window wide.
2. Tap the box. Its edge turns thick and blue; S M R X appear above its
   top-right corner (inside it when it is at the top of the window). They
   answer before the toolbox's anchor, which is right by the default place.
3. Drag from M. The box follows and stops at the window's edges.
4. Drag from R. The bottom-left corner stays put; the width follows, and the
   height — the subtitle size — follows too. Settings › Video shows the new
   size.
5. Tap anywhere else. The box is deselected and nothing else happens (a
   paused film stays paused, no page turns).
6. S opens the settings window on its Video page. X turns the subtitles off.
7. Tap the toolbox's Sub (CC) tile: "subtitles off"; tap again: back on, to
   the same track.
8. Double-tap the Sub tile: a list — Off, then every track, the one in use
   edged — opens above the tile without the subtitles flickering. A row
   chooses; a tap outside or Esc closes it.
9. Hold the Sub tile half a second: the list opens while it is still held.
10. Quit and open the film again: the box is where it was left
    (`layout.ini` `[subtitle_box]`), at the size it was given.
11. A DVD's or a Blu-ray's picture subtitles still appear where the disc put
    them; the Sub tile turns them off and on too.

## Measured on the Windows 11 VM, 2026-09-25

Fixture: a copy of `two_audio.mp4` as `film.mp4` with an English (two lines)
and a Korean `.srt` lasting the whole film.

| # | Result |
|---|---|
| 1 | PASS. Shaded box at the bottom, both lines inside. |
| 2 | PASS. Thick edge; S M R X at the top-right, over the T anchor. |
| 3 | PASS. Dragged 400 left and 300 up: the box stopped at x = 0 and rose 300. |
| 4 | PASS. Width 752 px, height about 140 px; Settings › Video read 42 pt, `settings.ini` `subtitle_size = 42`. |
| 5 | PASS. Deselected; the film stayed paused. |
| 6 | PASS. S: the settings window on Video, with "Subtitle box shade 35 %". X: "subtitles off". |
| 7 | PASS. "subtitles off", then back on. |
| 8 | PASS. Off / #1 eng / #2 kor, Off edged; choosing #2 showed "한국어 자막" in the moved box. |
| 9 | PASS. Photographed 0.8 s into the press: the list open, #2 kor edged. |
| 10 | PASS. `layout.ini` held `left = 0.0, bottom = 0.553191, width = 0.5875`; reopened, the box was there at 42 pt. |
| 11 | NOT RUN this time: the picture-subtitle path is unchanged (T078, T081). |
| touch | NOT RUN: the VM has no touch input; tap, double tap and hold were measured with a mouse. For a finger, the window now declines Windows' press-and-hold (which otherwise holds the press back and turns it into a right click) and flicks, so a held finger reaches the Sub tile as a held press. To check on a touch screen: hold the CC button — the list must open while the finger is still down. |
| detached toolbox, fullscreen | NOT RUN. |
