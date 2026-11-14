# Rubrapack (part)

- Source: Rubrapack, https://github.com/rubidus-api/rubrapack — MIT (see `LICENSE` here).
- Taken at: commit `a16048b` (v0.59.0), 2026-11-14 (vendored again at the owner's request, passed on by the
  Rubrapack session: the installer is built with 0.59.0, whose ready page names the folder and "for everyone / for
  you only" and whose status line is no longer cut). `rp_appx.h` changed since `d85da48` (v0.51.0): its strings go
  through `rp_wstr.h` (proven_c_lib's u16 views, from `vendor/proven/`) instead of `wcslen`, it waits up to an
  hour for a package and then cancels it. `LICENSE` is unchanged. Upstream's `src/menu/rubrapack_menu.c` moved its
  wide-string calls to `rp_wstr.h` since `d85da48` (no change in what it does); Rubraview's adapted copy keeps its
  own and does not take that.
- Files, unmodified:
  - `rp_appx.h` — from `src/ca/rp_appx.h`: registering and removing a package with an external
    location through Windows' PackageManager, in C.
  - `rp_wstr.h` — from `src/ca/rp_wstr.h`: the wide strings `rp_appx.h` uses, on proven_c_lib's u16 views.
- Adapted, not here: `src/menu/rubraview_menu.c` is Rubrapack's `src/menu/rubrapack_menu.c` changed
  for Rubraview (its own registry key, the `On` value, a list file for a long selection); the file's
  head says so.
- Used by: `src/pal/win32/pal_shellmenu_win32.c` (D-86).
- To update: copy the files again from a newer commit and note the commit here. Do not edit them in place.
