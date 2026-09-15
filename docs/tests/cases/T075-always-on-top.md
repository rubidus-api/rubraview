# T075 — Always on top, from the menu, the titlebar and a key

Covers: owner request 2026-09-15, R148 (§3.21.3)

## Steps and expected

1. Point at the top edge: the hover titlebar shows `Box · Pin · _ · [] ·
   [ ] · X` (`Box` puts the floating boxes back; it was always there but
   never drawn).
2. Click `Pin`: it is lit, and the window is on top of other programs'
   windows. Click again: unlit, not on top.
3. Open the menu: its first tile is `On top: off`; tap it — `On top: on`,
   on top again.
4. `Ctrl+Shift+T` does the same. The General page of the settings window
   has **Always on top**.
5. Quit and start again: it is still on top (`[general] always_on_top`).

## Measured on the Windows 11 VM, 2026-09-15

1–3 and the file by screenshot and the window's extended style read from
inside the session: `0x110` → Pin → `0x118` (`WS_EX_TOPMOST`) → Pin →
`0x110`; menu tile → `0x118` and `On top: on`; after quitting
`always_on_top = true`. Not measured: 4's key and 5's restart.

## Key and restart measured, 2026-09-16 (0.0.4)

`Ctrl+Shift+T` turned the viewer's extended style 0x110 → 0x118 (topmost).
Closed with Alt+F4, `settings.ini` held `always_on_top = true`, and the
viewer started again at 0x118. Killed instead of closed, the change is lost:
settings are written when the settings window closes and when the viewer
exits, not at each toggle.
