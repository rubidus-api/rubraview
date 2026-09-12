# T064 — The floating boxes stay where they were left

Covers: R041 (§3.6), RV-020

SPEC §3.6 says the boxes' positions are "persisted across sessions". They
are not settings anyone edits in a dialog — they are where the hands left
them — so they live in `layout.ini` beside the reading history, under the
same portable-or-AppData rule.

## Steps

1. Open any picture. Note where the menu box and the toolbox sit.
2. Drag the menu box's anchor bar to the middle of the window.
3. Quit with `Esc`, and look at `layout.ini`.
4. Start the viewer again.

## Expected

- Step 3: the file holds the four numbers, for example

  ```ini
  [boxes]
  toolbox_x = 804.0
  toolbox_y = 560.0
  menubox_x = 300.0
  menubox_y = 396.0
  ```

- Step 4: both boxes are where they were left, not back in their corners.
- A position saved on a larger screen does not put a box out of reach: it is
  pulled back inside the window, with room left to grab it.

## Measured on the Windows 11 VM, 2026-09-13 (v0.0.2)

The menu box dragged from the top-left corner to (300, 396), `Esc`, and it
came back at (300, 396) on the next run — screenshots before and after, and
the `layout.ini` above is the one the run actually wrote.
