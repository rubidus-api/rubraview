# T084 — One track running into the next: gapless and crossfade

Covers: SPEC §21, RFC-0001 §3.14.1, D-29, RV-075

A record is not a row of separate songs. When the next file in the folder
is a track, the viewer opens it while the current one still plays and
hands over at the end — with no gap, or across the crossfade the reader
set on the Audio page (0 to 5 s).

The timing is decided by `rubraview_track_plan`, which `test_music`
covers on the host: when to open the next one, when to start it, and what
each of the two should be worth while both are sounding (an equal-power
curve, so the loudness never dips). This case is about what comes out of
the speakers.

## Making tracks to test with

Two tones of different pitch, six seconds each, named so they sort in
order — `01 low.wav` at 440 Hz and `02 high.wav` at 880 Hz. A recording
of the session then says exactly which track is playing at any moment,
which music never could.

## Steps and expected

1. Open `01 low.wav` with `[audio] crossfade_seconds = 0`. At six seconds
   the pitch changes from 440 Hz to 880 Hz **with no silence between**,
   and the viewer moves to the second page — its name in the title, its
   own cover and words on screen.
2. Record the session across the change (`vmrecord.sh`) and read it with
   `wavstat.py`: there is no window below −60 dBFS at the join.
3. Set `crossfade_seconds` to 3 and play the first track again. For the
   three seconds before the end both tones are heard at once, the first
   fading and the second rising, and the level stays level — no dip in
   the middle, which is what an equal-power curve is for.
4. Stop the track by hand partway through, or move to another page: the
   track that was waiting is let go, and nothing plays underneath.
5. A film, or a page that is not a track, is never pre-opened: the next
   page has to be a music file.
6. Turn `gapless` off: the track ends and stops, as it did before.

## Measured on the Windows 11 VM

NOT RUN YET.
