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
| 2 | Press `PageDown`, then `PageDown` again | Advances to `page (9).jpg`, then `page (10).jpg` — natural order, not `(10)` before `(2)` (§3.2.3). (Written as `Right` until 2026-09-24: RFC-0003 gave the arrows to the window and to playback, and paging to `PageDown` / `Space` / `Enter`.) |
| 3 | Press `PageUp` repeatedly to reach the first page, then `PageUp` once more | Stops at `page (1).jpg`; it does not wrap or crash. |
| 4 | Press `Home`, then `End` | Jumps to the first, then the last page. |
| 5 | Press `1`, `2`, `3`, `4`, `5` in turn | Fit to window / width / height / 1:1 actual size / smart fit. **The window's own size and position must not change for any of them** (§3.4, R117). |
| 6 | Drag a window edge or corner | The window resizes, even though no border is visible (§3.21.1 hit-testing). |
| 7 | Maximize the window | It fills the work area and **does not cover the taskbar** (§3.21.1). |
| 8 | Press `F` (or `F11`) | True fullscreen covering the whole monitor, taskbar included. Press again to restore the previous size and position exactly. |
| 9 | `Ctrl` + mouse wheel | Zooms in and out. A plain wheel notch does **not** turn the page (owner, 2026-09-09: the wheel would otherwise mean two things depending on whether the picture overflows); it scrolls a picture that is taller than the window. |
| 10 | Press `4` (1:1), then zoom past 400%, then press `G` | A one-pixel grid appears over pixel boundaries. Press `G` again to remove it. It must not appear below 400% (§3.5). |
| 11 | Press `N` | Interpolation switches to nearest-neighbour: at high zoom the pixels become crisp squares rather than smooth (§3.5). |
| 12 | Open the sideways photo | It appears **upright**: the EXIF orientation tag was applied during decode (§3.9, RV-014/RV-029) — and **at once**, not seconds later (2026-09-22: turning the JPEG frame directly re-decoded it per row, 6.5 s for 1600x1200 on the VM; now 22 ms). |
| 13 | Press `B` | Layout cycles Single → Dual → Book. In Dual/Book two pages appear side by side with a small gap; in Book the first page stands alone as a cover (§3.3). |
| 14 | Press `M` while in Book mode | Reading order flips: the lower page number moves to the right side (manga order, §3.3). |
| 15 | Drag the window between two monitors with different scaling | The image stays sharp and correctly sized (§4.2 Per-Monitor V2). |
| 16 | Press `Escape` | The application exits cleanly. |

## Measured on the Windows 11 VM, 2026-09-24

Steps 1-14 and 16, with `page (1|2|9|10).png`, `photo.jpg` and a sideways
`sideways.jpg` in one folder. Evidence, in the order the steps run:

- 1: the window opened frameless on `page (2).png`, listed 2 of 6;
- 2-4: `PageDown` walked (1), (2), (9), (10) — natural order — `PageUp`
  came back and stopped at the first without wrapping; `Home` and `End`
  jumped to first and last;
- 5: the five fit keys left the window at `0,0 1280x752` exactly;
- 6: dragging the invisible right edge resized it to 1001x752;
- 7: `Win`+`Up` maximised to 1280x752, the work area — the taskbar stayed;
- 8: `F` filled 1280x800, the whole screen, and `F` again restored
  1280x752 to the pixel;
- 9: `Ctrl`+wheel took the zoom 121% → 146%; a plain wheel notch did
  nothing, which is what the owner asked for in 2026-09-09;
- 10: at 459% `G` changed 417,507 pixels (the grid); at 314% it changed
  none, so it keeps to its own rule;
- 12: the sideways photo stood upright at once (T078 measures the speed);
- 15: not run — this machine has one screen.

Two steps of this case were written for M2's keymap and were corrected
here rather than reported as faults: paging is `PageDown` / `PageUp`
since RFC-0003, and the plain wheel deliberately does not page.

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
