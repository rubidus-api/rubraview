# T089 — Thumbnails in the picker

Covers: RFC-0001 §3.15.2, D-34

Each tile of the file picker shows the item's picture, filling the tile,
slightly blurred and a little darkened; the name is drawn light with a dark
outline so it reads over any picture. A folder shows its first picture (its
first film when it has none).

Host coverage: `tests/test_thumb.c` (cut to the tile's shape, small, soft,
darker, opaque; refusals).

## Steps and expected

1. Open a folder of pictures and press `O`. The tiles fill in with the
   pictures within a moment; the names stay readable.
2. The pictures are soft, not sharp, and a little dark.
3. Pick a tile (Individual): it is tinted blue with a thick blue edge over
   its picture.
4. Go up (`..`): folders holding pictures show their first one with a small
   "[ folder ]" over it; a folder with only films shows its first film; a
   folder with neither keeps the plain tile. `..` itself has no picture.
5. Scroll a long listing: tiles that come into view fill in; the ones off
   screen are not made.
6. Archives (`.cbz`) keep the plain tile.

## Measured on the Windows 11 VM, 2026-09-25

| # | Result |
|---|---|
| 1 | PASS in `rbsmoke`: all six files filled in. The first attempt showed none — the shell's parser refuses a path joined with `/` (`C:\...\rbsmoke/page (1).png`); fixed by turning `/` into `\` before asking it. |
| 2 | PASS by eye (screenshot): soft and dimmed; names cream with a black outline. |
| 3 | NOT RUN. |
| 4 | PASS: `rbbatch` and `rbbgm` show their first picture, `rbpgs` its film, `cbz` and the profile folders stay plain. `rbmedia` stays plain: the first film in it is one the shell makes no thumbnail for. |
| 5 | PASS: after six wheel notches the rows that came into view filled in. |
| 6 | PASS (`cbz` folder's archives are plain). |
