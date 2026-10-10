# Rubrapack (part)

- Source: Rubrapack, https://github.com/rubidus-api/rubrapack — MIT (see `LICENSE` here).
- Taken at: commit `e8e96e2` (v0.39.0), 2026-10-10.
- Files, unmodified:
  - `rp_appx.h` — from `src/ca/rp_appx.h`: registering and removing a package with an external
    location through Windows' PackageManager, in C.
- Adapted, not here: `src/menu/rubraview_menu.c` is Rubrapack's `src/menu/rubrapack_menu.c` changed
  for Rubraview (its own registry key, the `On` value, a list file for a long selection); the file's
  head says so.
- Used by: `src/pal/win32/pal_shellmenu_win32.c` (D-86).
- To update: copy the file again from a newer commit and note the commit here. Do not edit it in place.
