# T086 — The mini player

Covers: SPEC §21, RFC-0001 §3.14.6 point 1, D-31, RV-081

`Shift+P` puts the music in a small window of its own, on top of whatever
is being read: the cover, the track and who made it, a strip showing
where it has got to, and three buttons — back a track, play or pause, on
a track. It drives whichever track is sounding: the background music if
there is any, otherwise the one the page itself is playing.

## Steps and expected

1. Open a track and press `Shift+P`. A window headed **Rubraview music**
   appears, about 320x80, above the viewer. `Shift+P` again puts it away,
   and so does `Esc` while it has the keyboard.
2. It shows the cover, the title and the artist the file carries — the
   file's own name when it has no title — the elapsed time and a strip
   filled as far as the track has played.
3. Press the middle button. The track pauses; the glyph becomes `>`.
   Press it again and it plays on.
4. Click along the strip. The track moves to that place.
5. Turn to a picture, so the music is background music, and press
   `Shift+P`: the little window shows **that** track and its buttons
   still drive it.
6. The outer buttons move to the track before and after it in the folder:
   with the music on the page, the viewer turns to it; with it in the
   background, the reader stays where they are and the music changes.
7. With nothing playing at all, the window says so rather than showing
   the last track's place.

## Measured on the Windows 11 VM, 2026-09-24

- `Shift+P` on a tagged MP3: the window opened above the viewer with the
  cover at the left, `달빛 아래에서`, `Rubra Quartet`, the strip and
  `00:10` — the Korean title renders, and the cover is decoded for that
  window rather than borrowed from the main one.
- The middle button: clicked at 00:10 on a finished track, it started
  from the beginning and the clock read `00:05` with the glyph `||`.
- The strip: clicked at four fifths of its width, the clock read `00:08`
  of a ten-second track.
- Background music: with the picture page on screen, the window showed
  `1 track.wav` at `00:33` of forty seconds, playing — the background
  track, not the page.
- Step 7 is why there is a "nothing is playing" line: a run where the
  track had ended left the window drawing the last position. It now says
  so instead.

Not measured: step 6 (the outer buttons), and pausing the background
music from the button. That run was read wrongly at the time — see the
end of T085: the VM key tool takes five to six seconds a call, so what
looked like "no sound at all" was a recording that had stopped before the
thing being measured happened.
