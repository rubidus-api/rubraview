# T071 — The anchors read M and T; Alt + wheel sets each box's opacity

Covers: RFC-0002 §6, D-15

## Steps and expected

1. Open a picture. The menu box's anchor reads **M**, the toolbox's **T**;
   the right half of each still reads `v`.
2. Hold `Alt` and turn the wheel six notches towards you over the menu
   box's anchor: OSD `menu box 60%`.
3. Hold `Alt` and turn it twelve notches over the toolbox: OSD `toolbox 30%`
   — it stops at 30 %, and the picture shows through the toolbox.
4. Press and release `Alt` alone, then `I`: the information bar toggles —
   the lone `Alt` did not put the window in menu mode.
5. Quit. `settings.ini` holds `[ui] menubox_opacity = 60`,
   `toolbox_opacity = 30`.

## Measured on the Windows 11 VM, 2026-09-15

All five, by screenshot, the information bar's bright pixels (455 → 160 →
455 with the lone `Alt` before the last `I`) and the file.

Seen on the way, not part of this change: the first key sent right after the
viewer starts is sometimes ignored (here and in the F2 check of T069).
