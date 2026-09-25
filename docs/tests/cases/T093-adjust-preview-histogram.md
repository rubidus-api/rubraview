# T093 — The adjust panel's live preview and histogram

Covers: RFC-0001 §3.13, RV-064, RV-065

`E` opens the adjust panel. The picture now follows every slider and curve
change: a copy of the page reduced to the window's size is run through the
same commit code Save a copy uses (`rubraview_edit_preview_size`,
`rubraview_edit_commit`), only when a value changed, and drawn over the
page. The crop is left out of the preview; its overlay shows it on the whole
picture. The curve widget shows the adjusted picture's histogram behind the
curve — luminance for RGB and Luma, the channel's own for Red, Green and
Blue — scaled to the tallest bin other than pure black and pure white.

Host coverage: the commit and histogram code are T002/T039's.

Before this, the panel changed only the values; the picture changed only in
the saved copy, although the manual said it followed the sliders.

## Steps and expected

1. Open a photograph and press `E`: the histogram shows behind the curve.
2. Drag Exposure right: the picture brightens as you drag; the histogram
   moves right. Left: darker, and it moves left.
3. Pick the Red curve: the histogram turns red and shows that channel.
4. Reset: the picture and the histogram are as at step 1.
5. Save a copy writes what the preview showed (at full size, crop applied).
6. Close the panel: the page is as it was.

## Measured on the Windows 11 VM, 2026-09-25

| # | Result |
|---|---|
| 1 | PASS: `scene.jpg` (a Windows wallpaper, 3840x2400): a dark peak with a long tail behind the curve. |
| 2 | PASS: on the test pattern `photo.jpg`, Exposure dragged right brightened blue and red at once; on `scene.jpg`, dragged left, the picture darkened and the histogram crowded to the left. |
| 3, 4, 5, 6 | NOT RUN. |
