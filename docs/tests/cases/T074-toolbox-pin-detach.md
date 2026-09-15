# T074 — The toolbox pinned open, and in a window of its own

Covers: RFC-0001 §3.6.1, RFC-0002 Q6, D-15

## Steps and expected

1. `Ctrl+T` (or Show › Detach toolbox): the toolbox leaves the viewer and
   becomes a small window on top — a `T` / `Dock` bar over the tiles of what
   is on screen, as see-through as the toolbox opacity says. Its tiles work;
   clicking them leaves the keyboard with the viewer.
2. Drag it by `T`: it goes anywhere, over or outside the viewer.
3. Dragging the toolbox's anchor past the viewer's edge detaches it too, and
   the same drag carries on moving the new window.
4. `Dock` (or `Ctrl+T` again): back in the viewer, open, where the window was.
5. `Shift+T` (or Show › Pin toolbox): the toolbox opens and its anchor's
   right half reads `*`; clicks elsewhere and `Esc` leave it open; its own
   `T` closes it and unpins it. The pin is kept in layout.ini.

## Measured on the Windows 11 VM, 2026-09-15

1, 2, 4 and 5 by screenshot: a detached video toolbox of 12 tiles, moved to
380,180; its Play tile started the film (`00:01.042 → 00:03.208`) and turned
to Pause; Dock brought it back at 380,180; pinned, it stayed open after a
click on the canvas. Step 3 (dragging the anchor out) is not measured: the
test viewer fills the screen, so there is no edge to drag past.

Changed from §3.6.1: a detached toolbox does not dock when dropped over the
canvas. A viewer that fills the screen is under it wherever it goes, so it
would dock at every move; it docks by `Dock`, `Ctrl+T` or the menu.

## Step 3 measured, 2026-09-16 (after 0.0.4)

The viewer was shrunk with Ctrl+arrows to 960x592, which found two faults
before the step could run:

- the toolbox anchor, left at the bottom-right of the larger window, stayed
  outside the smaller one and could not be reached. Anchors are now pulled
  inside the window (88 px across, 48 px down of room) whenever it resizes,
  the same rule layout.ini's positions get when they are read;
- the anchor followed the pointer only up to the window's edge: the window
  did not capture the mouse while a button was held, so past the edge it
  heard nothing and the toolbox never detached.

With both fixed, pressing the anchor's `T` at 912,564 and dragging to
1150,500 turned the toolbox into its own window under the pointer (`T` /
`Dock` header, eight archive tiles). A click on the canvas still turns the
page (2/2 → 1/2).
