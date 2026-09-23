# T082 — The music page: the cover, the backdrop and the track's words

Covers: SPEC §21, RFC-0001 §3.14.2.1 and §3.14.2.4, RV-076

A track has no picture of its own, so the viewer shows the record's: the
cover in the middle, the same cover blurred and dimmed behind it, and
what the file says about the track along the bottom. `src/core/tags.c`
reads all of it out of the file's own bytes — no decoder, no FFmpeg, the
same way the DVD and Blu-ray subtitles are read (D-22, D-27) — and
`test_tags` covers the reading. This case is about what reaches the
screen.

## Making a track to test with

Any music file with tags will do. One is easily made from a plain one:
put an ID3v2.3 tag in front of it holding `TIT2`, `TPE1`, `TALB`,
`TYER`, `TRCK` and an `APIC` picture, which is what the fixture below
was. Give the title non-ASCII text — it is the quickest way to see that
the encodings are handled.

## Steps and expected

1. Open the track. The cover fills the middle of the window.
2. Behind it, the same cover blurred and dimmed fills the whole window.
   A track with no cover has no backdrop, and the page looks as it did.
3. Along the bottom, above the seek bar and the status line rather than
   across them: the title and the artist; the record, its year and the
   track's number; then what the sound is — the codec, the bitrate, the
   sample rate and how many channels.
4. A line the file does not answer is left out rather than shown empty.
5. Open a film: none of this is drawn.

## Measured on the Windows 11 VM, 2026-09-24

An MP3 with an ID3v2.3 tag written for this: title `달빛 아래에서`
(UTF-8), artist `Rubra Quartet`, album `Night Pieces`, year 2026, track
`3/9`, and a 600x600 JPEG cover.

- The cover is shown at 600x600 in the middle;
- the backdrop is there — the window's edges carry the cover's colours,
  softened and dimmed, rather than the flat canvas;
- the three lines read `달빛 아래에서  —  Rubra Quartet`,
  `Night Pieces  (2026)   no. 3/9` and `MP3 · 64 kbps · 48.0 kHz ·
  stereo`. The Korean renders correctly, and the numbers are the ones
  the host reader gives for the same file.
- First try, corrected: the block was drawn across the seek bar and the
  status line. It is now placed from the bottom up, above both.

Not measured: a track whose cover the Windows shell cannot read (FLAC and
Opus often), where the cover comes from `tags.c` instead of the shell
thumbnail. The path is there and the reader is covered by `test_tags`.
