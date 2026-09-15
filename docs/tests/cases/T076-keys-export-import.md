# T076 — Keys exported to a file and imported from one

Covers: R148 (§3.22.2 "Export / Import")

## Steps and expected

1. `F10`, `Tab` to **Keys**. Under **Use keymap.ini** are two lines:
   `< Export keys to a file >` and `< Import keys from a file >`.
2. Export: a save dialog titled **Export keys**, file type **Keymap (*.ini)**.
   A name typed without an extension is saved as `.ini`; the file holds the
   keys on the page, in keymap.ini's form. The page says `keys exported`.
3. Import: an open dialog titled **Import keys**. The chosen file's keys
   replace the table (it grows or shrinks to them) and the page says
   `keys imported (Revert undoes this)`. A file with no keys changes nothing
   and says so.
4. **Revert** brings back the keys keymap.ini held when the window opened.
   Closed instead, the imported keys are written to keymap.ini like keys
   changed by hand.

## Measured on the Windows 11 VM, 2026-09-16 (0.0.4)

By screenshot and the files: 1 as described; 2 typed `a` → `Documents\a.ini`,
2211 bytes, starting `[ui]` / `toggle_fullscreen = "F, F11, Alt+Enter"`;
3 a file holding only `[navigation] next_page = "X"` left one row in the
table; 4 Revert brought the whole list back. Not measured: closing with the
imported keys, and a file with no keys.
