# T096 — Two pages at one height, and pictures too large for the card

Covers: D-39, RFC-0001 §4.1.3

Two-page view (`B`) used to lay the pages side by side at their own pixel
sizes and fit the pair, so a 600x900 scan beside an 1800x2700 one was a
third of its height. Now both are brought to the taller one's height
first; at actual size (`4`) each keeps its own pixels.

A page's texture was always made at the picture's full size, and a
picture past the device's largest bitmap or its memory failed to open.
Now it is at most the device's largest side and 128 megapixels, decoded
through WIC's scaler, halved again if the device still refuses; the page
keeps the picture's real size for layout, zoom, crop and the status line.

Host coverage: test_compositor ("A low-resolution page beside a high one
is brought to the same height", "At actual size each page keeps its own
pixels, centred") and test_viewport ("fit_within_limits ...").

## Fixtures

- `rbdual`: `01.png` 600x900 (blue, "LOW") and `02.png` 1800x2700 (red,
  "HIGH"), each with a 6-column grid.
- `rbhuge`: `huge.png` 20000x12000 (grey ramp, a red line every 1000 px
  across, green down, 20 px wide, a 1000x1000 blue square in the middle);
  `alpha.png` 18000x11000 RGBA (orange, a blue rectangle at alpha 96, a
  fully transparent square in the middle).

## Steps and expected

1. Open `rbdual\01.png`, press `B`: both pages the same height and width.
2. Open `rbhuge\huge.png`: it shows; the status line reads
   `20000 x 12000 | 100% | 1 / 1 | shown reduced`.
3. Press `4` (actual size): the blue square is 1000 screen pixels wide.
4. Press `1`, then `E`: the preview and histogram appear, colours right.
5. Open `alpha.png`: the half-transparent rectangle is blended over the
   canvas (not bright blue), the hole shows the canvas.

## Measured on the Windows 11 VM, 2026-09-25

| # | Result |
|---|---|
| 1 | PASS: LOW and HIGH each about 500x750 on screen, side by side. |
| 2 | PASS: status line as expected. The build before this (`de5a748`) also opened it at full size: the VM's software renderer took a 20000x12000 bitmap, so the old failure could not be shown here, and the halving step after a refusal did not run. The new build held it at 128 MP (about 512 MB instead of 960). |
| 3 | PASS: the square spans x 140 to 1140. |
| 4 | FAIL, then PASS: red and blue were swapped in the preview (WIC's scaler put after the RGBA converter emits BGRA); the scaler now comes first, the conversion after, in the preview and the page paths alike. |
| 5 | PASS: the rectangle came out about (10,10,106) over the #111 canvas, the premultiplied blend of blue at 96/255; the hole showed the canvas. |
