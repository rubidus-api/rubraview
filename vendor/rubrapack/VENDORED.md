# Rubrapack (part)

- Source: Rubrapack, https://github.com/rubidus-api/rubrapack — MIT (see `LICENSE` here).
- Taken at: commit `d85da48` (v0.51.0), 2026-10-30 (vendored again on the owner's word: "rubrapack이 업그레이드
  되었습니다. 앞으로의 패키징은 새 벤더링으로"). `rp_appx.h` changed since `f0f7cbd` (v0.47.0): `external` may be
  NULL (a package holding its own files), which Rubraview does not use; `LICENSE` is unchanged. Upstream's
  `src/menu/rubrapack_menu.c` has gained `List` and `HideWhen` since `e8e96e2`; Rubraview's adapted copy has its
  own list file and does not take them.
- Files, unmodified:
  - `rp_appx.h` — from `src/ca/rp_appx.h`: registering and removing a package with an external
    location through Windows' PackageManager, in C.
- Adapted, not here: `src/menu/rubraview_menu.c` is Rubrapack's `src/menu/rubrapack_menu.c` changed
  for Rubraview (its own registry key, the `On` value, a list file for a long selection); the file's
  head says so.
- Used by: `src/pal/win32/pal_shellmenu_win32.c` (D-86).
- To update: copy the file again from a newer commit and note the commit here. Do not edit it in place.
