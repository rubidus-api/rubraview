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
| 3 | FAIL, then PASS: there was no way to pick a channel (`active_channel` was never set after the session began). The label is now a button (`rubraview_edit_cycle_channel`, host-tested in test_edit); a click showed "Red >" and a red histogram. |
| 4 | PASS: the Red curve lifted turned the page magenta as it was dragged; Reset brought back "RGB >", the grey histogram and the page as at step 1. |
| 5 | FAIL, then PASS: on the 3840x2400 page the copy came out unedited — the red values of `scenep_edit.png` (a PNG, so lossless) equalled the source's at every level. The commit's first copy did not fit in the 64 MB app arena left after decoding, and the save wrote the decoded page instead. With memory sized to the picture: red mapped one-to-one (128→208, 30→52), green and blue unchanged, 3840x2400, the same means as the preview's; an Exposure save came out brighter. The notice "saved a copy beside the page" shows (it had been spent while the save held the frame). Crop not tried. |
| 6 | PASS: Save a copy closes the panel; the page is drawn as it was. |
