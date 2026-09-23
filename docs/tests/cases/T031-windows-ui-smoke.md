# T031: Windows reading-UI smoke check (M3)

M3 added the reading UI: the two floating boxes, the OSD, the hover
titlebar, the slide show, non-destructive rotation and the filmstrip.
Their *decisions* — hit testing, timers, menu navigation, pointer intent
— are covered on the host by T026-T030. What no gate on the build
machine can show is whether those decisions reach the screen correctly,
because there is no Windows here.

Run this after T025 (which covers the M2 canvas itself) on the Windows
target.

## Prerequisites

- `dist/rubraview-v<version>.exe` from `make win64`.
- A folder of at least a dozen images, plus one photo taken with the
  camera held sideways.

## Steps and expected results

| # | Action | Expected |
|---|---|---|
| 1 | Open an image and move the mouse | A status line appears at the bottom with the file name, pixel size, zoom percent and `n / total`, then fades out after about two seconds of stillness (§3.1). |
| 2 | Press `I` | The status line stays on permanently. Press `I` again to return it to auto-fade. |
| 3 | Move the pointer to within ~12 px of the top edge | The titlebar slides in over the canvas showing the file name and four controls at the right (§3.21.2). |
| 4 | Move the pointer back into the canvas | The titlebar disappears about half a second later, not instantly. |
| 5 | With the titlebar shown, hover the rightmost control | It highlights crimson; clicking it closes the application (§3.21.3). |
| 6 | Click the other three controls in turn | Minimize, maximize/restore, fullscreen — each behaves as its glyph says. |
| 7 | Drag the empty middle of the titlebar | The window moves, and Windows Aero Snap still works when dragged to a screen edge. |
| 8 | Press `T` (`F2` renames since D-14) | The toolbox opens as a grid of square tiles near the bottom right. Press again to close. |
| 9 | Hover over the toolbox anchor without clicking | It expands on hover, and collapses again about half a second after the pointer leaves (§3.6.3). |
| 10 | Click a toolbox tile such as `Next` or `Zoom+` | The action fires: the page advances, the image zooms. |
| 11 | Press `Tab` (or `F1`) | The menu box opens at the top left with a breadcrumb reading `Menu` above it (§3.6.2). |
| 12 | Drag the menu box toward a screen corner | It stops at the client edge — its whole body stays inside the window, never spilling out (§3.6.2). |
| 13 | Drag the toolbox off the window edge | Unlike the menu box, it is allowed past the edge (the detach path, §3.6.1). Dragging it back over the canvas docks it again. |
| 14 | Press `F4` | The filmstrip appears along the bottom; already-viewed pages show as thumbnails, and the strip follows the current page as you flip. |
| 15 | Press `R` several times | The image rotates 90° clockwise each time, and the window and its zoom stay put; four presses return to the original (§3.9). |
| 16 | Press `H`, then `R` | With the image mirrored, `R` must still *look* clockwise on screen — the case T026 pins down on the host. |
| 17 | Press `S` (or `F5`) | The slide show starts, going fullscreen; pages advance about every three seconds. |
| 18 | Leave the mouse still during the slide show | The pointer disappears after ~1.5 s and returns the moment the mouse moves (§3.2.5). |
| 19 | Press `S` again | The slide show stops and the cursor comes back. |
| 20 | Click the left 30% / right 30% / middle of the canvas | Previous page / next page / toggle the overlay. In Book mode with `M` pressed (right-to-left), the two sides swap roles (§3.7.3). |
| 21 | Side mouse buttons, `Shift` + wheel, `Ctrl` + wheel | Previous/next page, 10-page skip, cursor zoom (§3.7.3). |
| 22 | Create a `keymap.ini` next to the exe with `[navigation]` and `next_page = N`, then restart | `N` now advances the page: the file replaced the built-in bindings (§3.7.5, RV-038). |
| 23 | Open the menu box (`Tab`) and tap `Layout` | The grid drills into `Single / Dual / Book`, tile 0 becomes `< Back`, and the breadcrumb reads `Menu > Layout` (§3.6.2). |
| 24 | Tap `Dual`, then reopen and tap `< Back` | The layout switches to dual pages; Back returns to the category level. |
| 25 | Press `O` | The Metro file picker fills the window: breadcrumb chips across the top, large folder/file tiles, and a bottom bar with the item count (§3.15.2). |
| 26 | In the picker, press a letter | The focus jumps to the next entry starting with it; pressing the same letter again cycles to the following match (§3.15.4). |
| 27 | Tap a folder tile, then tap an earlier breadcrumb chip | Entering a folder relists it; the chip navigates straight back to that level. |
| 28 | Tap an image tile (or focus it and press `Enter`) | The picker closes and that image opens, with its siblings indexed as usual. Press `Esc` instead to close without opening. |
| 29 | On a touch screen: pinch, and drag with two fingers | Pinch zooms about the point between the fingers; a two-finger drag pans (§3.6.5). On a machine with no digitiser this step is not applicable. |

| 30 | Press `L`, then flip pages between images of different sizes | With Fit Lock on the zoom and fit mode carry across; with it off each page refits (§3.4). |
| 31 | Press `Ctrl+1` | The image stretches to fill the window, ignoring aspect ratio (§3.4). |
| 32 | Hold `Shift` and press `Right` / `Left` | Skips ten pages at a time (§3.7.2). |
| 33 | Press `Alt` with the arrow keys while zoomed in | The viewport pans. `A`, `D` and `S` stay page-back, page-forward and slide show — the RFC double-books them, and the primary binding wins. |
| 34 | Start the slide show, then press `]` and `[` | The interval moves in 0.5 s steps; with `Shift` held, 0.1 s steps (§3.2.1). |
| 35 | Press `Shift+B` on a chapter containing a wide double-page scan | Pre-merged spread detection toggles: with it off the wide page pairs like any other (§3.3.4). |
| 36 | Press `Backspace` | The picker opens on the parent directory (§3.7.2). |

Every binding above is also checked mechanically by T032 on the host, so
a failure here points at the action's *effect*, not at the key lookup.

## Known M3 limitations (not defects)

- Filmstrip cells reuse full-size page textures for pages already
  decoded; dedicated low-resolution thumbnail decoding waits for the
  asynchronous pre-cache worker in M4 (RV-044), which is where async
  decode belongs.
- Slide-show transitions compute their progress (T029) but the canvas
  still cuts between pages; the cross-fade needs the Direct2D 1.1 device
  context that arrives with RV-064 in M6.
- The menu box carries the §3.6.2 category tree for the actions that
  exist today. Categories whose features are not built yet — Adjust &
  Filter (M6), Playlist & Bookmarks, Batch Export, Settings (M9) — join
  the tree as those milestones land; the navigation itself is complete.
- The picker selects one file at a time. Its multi-select model is
  implemented and tested (T030) but no gesture binds it yet, since the
  actions it would feed (`Play as Slideshow`, `Create Playlist`) belong
  to later milestones.

## Recording the result

Note the Windows version and which steps passed. A failure in 1-7 points
at the chrome drawing path (DirectWrite or the fill/stroke primitives);
8-13 at the box wiring in `main.c`, since the geometry itself is proven
by T028; 15-16 at the orientation wiring, proven by T026.


## Floating boxes: the two-button anchor and putting them back (2026-09-10)

The owner asked for the collapsed anchor to be two square buttons side
by side — the left opening the box only when clicked, the right opening
it on hover — and for a way to bring both boxes back when they have been
dragged somewhere unreachable.

| # | Action | Expected |
|---|---|---|
| 1 | Look at either collapsed box | It is a **wide bar, not a square**: two buttons. The left is outlined, the right is filled — they must not look like one control. |
| 2 | Rest the pointer on the **left** button without clicking | **Nothing opens.** Wait a few seconds; still nothing. |
| 3 | Rest the pointer on the **right** button | It expands on its own. Move away: it collapses after about half a second. |
| 4 | Click the **left** button | It opens and **stays open** with the pointer elsewhere. Click again: it closes. |
| 5 | Hover the right button to open it, then click that same button | It stays open when the pointer leaves — the click means "keep it". |
| 6 | Drag a box far off the window, then move the pointer to the very top | The hover titlebar appears. The **leftmost** icon in the top-right group is the put-them-back button. |
| 7 | Press it | **Both** boxes reappear: the menu box at the **top-left**, the toolbox at the **bottom-right**, wholly inside the window, and a line says so. |
| 8 | Drag the toolbox right out of the window so it detaches, then press the button | It comes back inside and is docked again. |
| 9 | Make the window very small and press the button | The boxes are still on screen — at minimum their top-left corner, which is the part with the buttons on it. |
| 10 | Resize the window, then press the button again | They land in the corners of the *new* size. |

The placement rules, the two halves and the corner each box goes home to
are checked on the host by T009 and T030; what needs a machine is
whether the two buttons are visibly different and whether the hover feels
right.

## Measured on the Windows 11 VM, 2026-09-24

The first run of this case. The build was 0.0.17 run from its own folder,
on `rbtest` (six images) at 1280x752. Keys were sent into the interactive
session; the steps that need a modifier (`Ctrl`, `Shift`, `Alt`), a drag,
or a hover are not reachable that way and are marked so.

| # | Result |
|---|---|
| 1, 2 | PASS. The line reads `a_photo.jpg \| 4032 x 3024 \| 100% \| 1 / 6`, and `I` pins it on. |
| 8 | PASS, with the step's words now stale: since D-20 the toolbox opens as a **horizontal strip** — name row, then icon buttons — not a grid of tiles, and the pin sits beside the anchor (D-20, owner 2026-09-22). |
| 11 | PASS. The menu opens at the top left with the breadcrumb `Menu` and the tiles `On top`, `File >`, `View >`, `Show >`, `Help >` (D-18's tree, not the older one this step describes). |
| 14 | PASS. The filmstrip appears along the bottom with thumbnails of the pages already decoded, and the current page is in it. |
| 15, 16 | **FAIL — see the defect below.** |
| 25-28 | NOT RUN (the picker's own case is T079, measured 2026-09-23). |
| 17-19 | NOT RUN this pass. |
| 29 | Not applicable: the VM has no digitiser. |
| 7, 12, 13, 20, 21, 29, 33 | NOT RUN: a drag, a hover or a modifier, none of which the in-session key path can send. The boxes' own placement is covered on the host by T009 and T030. |
| Boxes 1 | PASS by eye: each collapsed box is a wide bar of two buttons — the left outlined, the right filled — not one square. |

Also seen: `F1` opens the help window and it lists 95 keys, grouped
(T080 again, on this build); `E` opens the edit workbench and `Esc`
closes it.

### Defect found by this case: rotation and flipping do nothing (2026-09-24)

`R`, `H` and the other keys of the "Looking at a picture" group change
nothing on screen. This is not the key lookup and not the core:

- the F1 help, which is built from the keymap in force, lists `R` as
  Rotate, and a host check of `rubraview_keymap_find_action` returns
  `rotate_cw` for `R` in the `view` context;
- `rubraview_orientation_rotate_cw` and `rubraview_orientation_matrix`
  are right on the host (one press turns 800x600 into 600x800 with the
  matrix `0 1 -1 0 600 0`);
- the action *is* reached: pressing `R` wakes the OSD, which only
  `handle_action` does — 18200 pixels of the status strip change and not
  one pixel of the picture;
- binding `rotate_cw` in the global section of a `keymap.ini` beside the
  program changes nothing either, so it is not the context;
- measured on both a large photo (4032x3024) and a small one (800x600),
  windowed, with the same result: the canvas is identical to the pixel
  before and after.

So `app->orientation` is set and the drawing does not follow it. The next
step is to instrument `draw_spread` — it is the only place that consults
the orientation, and either it is not the path this page takes or the
transform it builds is discarded. Recorded in `BACKLOGS.md`.
