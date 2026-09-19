# T045: Windows editing, export and batch check (M6)

M6 gave the viewer an editing workbench, file export, a batch mode and a
new rendering engine. Everything that *decides* something is covered on
the host by T039-T044 and T014. Three things are not, and cannot be from
here:

- that the new Direct2D 1.1 device actually brings up a window at all —
  the renderer was rebuilt underneath every milestone that came before,
  so **this case re-runs T025's canvas checks first**;
- that an image comes out of WIC's encoders correctly;
- that a batch run does the right thing to a real directory.

Run after T025, T031 and T037.

## Prerequisites

- `dist/rubraview-v<version>.exe` from `make win64`.
- A folder of 30-50 JPEGs, backed up. The batch steps write files.
- One photo with GPS in its EXIF, and a hex viewer.
- A machine with no dedicated GPU, or one where the driver can be
  disabled, for step 3.

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | Open a folder and repeat every step of T025 | Everything that worked before still works. The renderer changed completely (D2D 1.0 → 1.1 on a swap chain); if anything regressed, it regressed here. |
| 2 | Zoom a photo to 300% and compare against the previous build | Smoother. Cubic interpolation was silently falling back to linear before; it is real now. Pixel art with `N` must still be perfectly crisp. |
| 3 | Run on a machine with no usable GPU driver | It still starts and shows images — the renderer falls back to WARP, the software rasteriser. |
| 4 | Start a slide show with a 2-3 second interval | Pages **cross-fade** rather than cutting. This is the M3 item that had no way to be drawn until now. |
| 5 | Press `E` | The adjust panel appears on the right. Drag Exposure: the image follows. Drag past the end: the slider stops rather than wrapping. |
| 6 | Drag a slider and pull the pointer far off the panel | The slider keeps following horizontally; it does not drop when the pointer leaves the row. |
| 7 | Set Black point above White point | The other one moves out of the way instead of the two crossing. |
| 8 | Press `Reset`, then `Esc` | Everything returns to neutral; `Esc` closes the panel without closing the program. |
| 9 | Adjust something, press `Save a copy` | `<name>_edit.<ext>` appears beside the original. The original is untouched — check its timestamp and size. |
| 10 | Press `Ctrl+E`, choose PNG, `Export` | A PNG appears beside the original and opens correctly in another viewer. |
| 11 | With the GPS photo: `Ctrl+E`, tick Privacy clean, leave the format alone, `Export` | The written file has **no APP1 segment** (search the hex for `FF E1`), and is otherwise byte-identical to the original from the start-of-scan marker onwards — nothing was re-encoded. |
| 12 | `rubraview.exe --batch --resize=50% --format=webp <folder>` from a command prompt | It runs without opening a window, prints a "converted / skipped / failed" line, and returns to the prompt. |
| 13 | `rubraview.exe --batch --resiez=50% <folder>` (deliberate typo) | It refuses, names the option it did not understand, and **converts nothing**. |
| 14 | `rubraview.exe --batch --rotate=90 --include=*.jpg <folder>` | JPEGs are rotated. Compare a result against the original at high zoom: it must be **identical**, not slightly softer — the rotation went through the coefficients, not through a decoder. |
| 15 | `rubraview.exe --batch --resize=50% --out=D:\out <folder>` on a folder of 2000 files | Memory in Task Manager stays flat rather than climbing with the file count (§3.11's per-file arena reset). |
| 16 | Open a `.ico` you exported with `Ctrl+E` | Windows Explorer shows it correctly at several sizes — 16, 32, 48 and 256 are all inside the one file. |

## Known limitations (not defects)

- **WebP cannot be written** unless Windows has a WebP encoder codec
  installed. It fails rather than writing a file with the wrong
  contents. Reading WebP works.
- **The preview is not a GPU effect graph.** §3.13 describes one;
  MinGW's C headers do not expose `ID2D1Effect`, so the preview runs the
  same commit code on a reduced copy instead. It has an advantage the
  effect graph would not: the preview and the saved result come from one
  implementation and cannot disagree. Watch for the preview lagging on a
  very large image and report it if it does.
- **The crop rectangle has no drag overlay yet.** The crop model — 
  normalisation, aspect locks, staying inside the image — is implemented
  and tested (T039); what is missing is the mouse overlay to drive it, so
  only the ratio choice is reachable from the panel.
- **The curve widget is not drawn.** Same situation: the curve model is
  complete and tested (T039), the widget is not built.
- ~~**The batch dialog's `Run` button closes the dialog without running.**~~
  Built 2026-09-20 (RV-068) and measured on the VM: with **Resize %** near
  50, `Run on this folder` wrote three files into `<folder>\rubraview-out\`
  at exactly half the pixels (4032x3024 → 2016x1512, 1600x1200 → 800x600,
  800x600 → 400x300); with **Format** on PNG it wrote `a_photo.png` and
  `b_exif6.png`, PNG bytes under PNG names. Not measured: a folder large
  enough to show how long the viewer stays unresponsive, and Grayscale and
  Privacy clean through the dialog.
