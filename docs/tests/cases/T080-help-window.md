# T080 — The help window (F1)

Covers: D-24

## Steps and expected

1. `F1`: a window titled "Rubraview help - keys" opens beside the viewer.
   The viewer keeps working — turn a page with the help still on screen.
2. It opens with two lines of explanation, then groups: Everywhere,
   Moving about, Looking at a picture, Film and music, and so on. Each
   line is the keys on the left and what they do on the right, in the
   words the menu uses.
3. The wheel, `PageUp`, `PageDown`, `Up`, `Down`, `Home`, `End` scroll it.
4. `F1` again, or `Esc`, closes it. `F1` once more opens it where it was.
5. Change a key on the settings window's Keys page, then open the help:
   the new key is shown.
6. Nothing overlaps: an action with three keys (`PageUp, Backspace,
   Shift+Space`) still has its description clear of them.

## Measured on the Windows 11 VM, 2026-09-23

By screenshot: the window opened beside the viewer at 860x720 with 95
keys listed and the footer "95 keys | wheel or PageUp/PageDown scrolls |
F1 or Esc closes". `PageDown` moved a page down the list. Steps 5 and 6:
the second column starts after the longest key list there is, which was
measured after it was found overlapping at a fixed column.

The keys the window shows and the tables in `README.md`, `README.ko.md`,
the manual and RFC-0001 all come from `src/core/default_keymap.c` — the
window reads it at run time, the tables are written by
`scripts/check-actions.py --write`, which fails when the two disagree.
