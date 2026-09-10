# T025: Windows canvas smoke check (M2)

Milestone M2 is the first one that produces a running executable, and it
is the first one this workstation cannot verify: there is no Windows
here. The cross-compiler proves the code builds and links
(`make win64` → `dist/rubraview-v<version>.exe`, a PE32+ GUI binary), and the host
test suite proves the portable half (layout, viewport maths, compositor,
keymap, sibling indexing, clock). What no automated gate covers is
whether a window actually appears and an image actually renders.

This procedure is that missing check. Run it on the Windows target.

## Prerequisites

- Windows 10 version 1703 or later (for Per-Monitor V2 DPI; the binary
  starts on older versions but falls back to system DPI scaling).
- `dist/rubraview-v<version>.exe`, built with `make win64`.
- A folder holding at least four images named so natural ordering
  matters, e.g. `page (1).jpg`, `page (2).jpg`, `page (9).jpg`,
  `page (10).jpg`. At least one JPEG carrying an EXIF orientation tag
  other than 1 (a photo taken with a phone held sideways).

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | `rubraview.exe "page (2).jpg"` | A window opens with **no title bar and no border** — the image reaches every edge (§3.21.1). `page (2).jpg` is displayed. |
| 2 | Press `Right`, then `Right` again | Advances to `page (9).jpg`, then `page (10).jpg` — natural order, not `(10)` before `(2)` (§3.2.3). |
| 3 | Press `Left` repeatedly to reach the first page, then `Left` once more | Stops at `page (1).jpg`; it does not wrap or crash. |
| 4 | Press `Home`, then `End` | Jumps to the first, then the last page. |
| 5 | Press `1`, `2`, `3`, `4`, `5` in turn | Fit to window / width / height / 1:1 actual size / smart fit. **The window's own size and position must not change for any of them** (§3.4, R117). |
| 6 | Drag a window edge or corner | The window resizes, even though no border is visible (§3.21.1 hit-testing). |
| 7 | Maximize the window | It fills the work area and **does not cover the taskbar** (§3.21.1). |
| 8 | Press `F` (or `F11`) | True fullscreen covering the whole monitor, taskbar included. Press again to restore the previous size and position exactly. |
| 9 | `Ctrl` + mouse wheel | Zooms in and out. Plain mouse wheel moves to the next/previous page. |
| 10 | Press `4` (1:1), then zoom past 400%, then press `G` | A one-pixel grid appears over pixel boundaries. Press `G` again to remove it. It must not appear below 400% (§3.5). |
| 11 | Press `N` | Interpolation switches to nearest-neighbour: at high zoom the pixels become crisp squares rather than smooth (§3.5). |
| 12 | Open the sideways photo | It appears **upright**: the EXIF orientation tag was applied during decode (§3.9, RV-014/RV-029). |
| 13 | Press `B` | Layout cycles Single → Dual → Book. In Dual/Book two pages appear side by side with a small gap; in Book the first page stands alone as a cover (§3.3). |
| 14 | Press `M` while in Book mode | Reading order flips: the lower page number moves to the right side (manga order, §3.3). |
| 15 | Drag the window between two monitors with different scaling | The image stays sharp and correctly sized (§4.2 Per-Monitor V2). |
| 16 | Press `Escape` | The application exits cleanly. |

## Known M2 limitations (not defects)

- `RUBRAVIEW_INTERP_CUBIC` and `HIGH_QUALITY_CUBIC` currently render as
  linear: `ID2D1HwndRenderTarget` (Direct2D 1.0) offers only nearest and
  linear. The cubic modes arrive with the Direct2D 1.1 device context
  that the effect graph needs in M6 (RV-064). Nearest — what pixel art
  depends on — is exact today.
- Pages load synchronously on demand; the asynchronous pre-cache ring is
  RV-044 in M4, so the first flip to an unseen page may pause briefly.
- No on-screen display, filmstrip, toolbox, or menu box yet: those are
  M3 (RV-039, RV-042, RV-020).
- Only the built-in keymap is active; reading `keymap.ini` from disk is
  wired in M3 (RV-038). The bindings themselves already come from the
  RV-030 parser, so the file format is the one this table exercises.

## Recording the result

Note the Windows version, the GPU, and which steps passed. A failure in
steps 1-3 points at the window or WIC backend; a failure in 5-7 at the
frameless shell; a failure in 13-14 at the layout engine, which is
already covered by T013 on the host and so is more likely a wiring bug in
`src/app/main.c`.
