# T097 — Zooming into a page shown reduced is sharp (tiles)

Covers: D-40, D-39

A page too large for the graphics card is held as one reduced texture
(D-39). Zoomed in past it, the part on screen is decoded again from the
file in 512-pixel tiles on a thread of its own and drawn over it.

Host coverage: test_tiles (when tiles are wanted, the level, a tile's
rectangle and output size, the tiles a view meets middle first, the
matrix inverse).

## Fixture

`rbfine\fine.png`, 20000x12000: grey 90 with a 3000x2000 one-pixel
black-and-white checkerboard in the middle. Blurred, the checkerboard is
flat grey; sharp, every pixel is 0 or 255 (standard deviation 127.5).

## Steps and expected

1. Open `fine.png`, press `4` (actual size): the middle of the screen is
   the checkerboard itself, pixel for pixel, within a moment.
2. The same with the build before tiles (`f351104`): grey mush — the
   reduced texture magnified.
3. Drag the picture a long way: the part that comes into view is sharp
   too.
4. No seam where tiles meet.

## Measured on the Windows 11 VM, 2026-09-25

| # | Result |
|---|---|
| 1 | PASS: the screen's middle (400x300) had mean 127.5 and standard deviation 127.5 at 0.3 s after `4` and at 5 s — the checkerboard exactly. |
| 2 | PASS (the difference): `f351104` gave standard deviation 29.7 for the same place. |
| 3 | PASS: after an 800-pixel drag all four corners of the screen had standard deviation 126-127.5 already 0.15 s after release (the half-tile margin had fetched most of it). |
| 4 | PASS: no column or row of the screenshot lost contrast (per-line standard deviation below 100 only at the window's own edge pixels). |

Not run: coarser levels (level 1+ needs a picture several times larger
than the budget; host-tested), an EXIF-turned huge picture, an archive
page (not tiled).
