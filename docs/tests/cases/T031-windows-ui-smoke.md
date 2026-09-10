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
| 8 | Press `T` (or `F2`) | The toolbox opens as a grid of square tiles near the bottom right. Press again to close. |
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
