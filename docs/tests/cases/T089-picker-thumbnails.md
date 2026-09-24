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
6. A comic archive shows its first page. One whose first page is over
   24 MB, or that cannot be read within about 1.5 s at the speed seen, keeps
   the plain tile — and the picker never waits for it.
7. Close the viewer while thumbnails are being made: it exits at once.

## Measured on the Windows 11 VM, 2026-09-25

| # | Result |
|---|---|
| 1 | PASS in `rbsmoke`: all six files filled in. The first attempt showed none — the shell's parser refuses a path joined with `/` (`C:\...\rbsmoke/page (1).png`); fixed by turning `/` into `\` before asking it. |
| 2 | PASS by eye (screenshot): soft and dimmed; names cream with a black outline. |
| 3 | NOT RUN. |
| 4 | PASS: `rbbatch` and `rbbgm` show their first picture, `rbpgs` its film, `cbz` and the profile folders stay plain. `rbmedia` stays plain: the first film in it is one the shell makes no thumbnail for. |
| 5 | PASS: after six wheel notches the rows that came into view filled in. |
| 6 | Superseded by D-35 (measured below). |

## Measured again on the Windows 11 VM, 2026-09-25 (D-35: background thread, archives)

Fixture `rbthumbs`: 300 pictures, a film, `Vol 01.cbz`, `manga.cbz`, and
`a_bigpage.cbz` whose first page is a 30 MB stored entry.

| # | Result |
|---|---|
| 1 | PASS: 305 items listed; every tile on screen had its picture in the first photograph (a capture takes about 1.8 s, longer than the ~20 visible thumbnails take, so the filling-in itself could not be photographed). |
| 5 | PASS: 120 wheel notches, then another 300: the rows arriving were filled by the next photograph. |
| 6 | PASS: `manga.cbz` and `Vol 01.cbz` show their first page; `a_bigpage.cbz` keeps the plain tile (entry over the limit). The time-budget give-up is host-tested (`test_thumbq`), not measured: the VM's disk is too fast to be slow. |
| 7 | PASS: Esc Esc with the picker open; no rubraview process four seconds later. |
