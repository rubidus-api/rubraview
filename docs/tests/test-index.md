# Test Index

Short authoritative TDD catalog.

Read this before implementing or changing behavior. Open detailed case files only when relevant.

| ID | Requirement | Purpose | Command | Detail | Status |
|---|---|---|---|---|---|
| T000 | bootstrap | Confirm test catalog is initialized | manual review | docs/tests/cases/T000-bootstrap.md | active |
| T001 | R107 | Verify `rubraview_pixbuf_t` allocation, bounds, clone, crop, and convert | `make test` | tests/test_pixbuf.c | active |
| T002 | R104, R121 | Verify sRGB <-> Linear RGB LUT, curves, levels, and color adjust | `make test` | tests/test_color.c | active |
| T003 | R104, R124 | Verify image resampling kernels (Bilinear, Bicubic, Lanczos) | `make test` | tests/test_resample.c | active |
| T004 | R104, R123 | Verify spatial convolution (Gaussian blur, unsharp mask) | `make test` | tests/test_filters.c | active |
| T005 | R134 | Verify `u8str_t` zero-copy path deconstruction and null-term invariant | `make test` | tests/test_path.c | active |
| T006 | R112 | Verify natural/lexical name comparison and array sorting | `make test` | tests/test_sort.c | active |
| T007 | R136 | Verify strict UTF-8 decode/encode/validate (overlong, surrogate, truncated) | `make test` | tests/test_utf8.c | active |
| T008 | R106, R112 | Verify `*`/`?` glob matching and `;`-separated pattern lists | `make test` | tests/test_glob.c | active |
| T009 | R137 | Verify INI parse/serialize, typed getters, section grouping | `make test` | tests/test_ini.c | active |
| T010 | R136 | Verify Hangul Jamo composition and Latin combining-mark NFC fold | `make test` | tests/test_nfc.c | active |
| T011 | R136 | Verify archive filename UTF-8-flag/validation encoding verdicts | `make test` | tests/test_encoding.c | active |
| T012 | R110 | Verify six fit modes and viewport affine matrix composition | `make test` | tests/test_viewport.c | active |
| T013 | R109, R119 | Verify Single/Dual/Book pagination, pre-merged spreads, auto-collapse | `make test` | tests/test_layout.c | active |
| T014 | R118 | Verify ZIP EOCD/CD/local-header parsing and zero-copy stored-entry reads | `make test` | tests/test_archive.c | active |
| T015 | R140 | Verify ComicInfo.xml Manga/Title/Series/Volume/Page parsing | `make test` | tests/test_comicinfo.c | active |
| T016 | R138 | Verify byte-budgeted two-tier LRU touch/evict/remove | `make test` | tests/test_lru.c | active |
| T017 | R113, R146 | Verify EXIF orientation read and lossless JPEG privacy marker strip | `make test` | tests/test_exif.c | active |
| T018 | R120 | Verify keymap.ini parsing and context-aware reverse key-combo lookup | `make test` | tests/test_keymap.c | active |
| T019 | R112, R128 | Verify slideshow dwell timing, loop policies, pause/resume | `make test` | tests/test_slideshow.c | active |
| T020 | R106 | Verify batch file matching and naming-pattern token substitution | `make test` | tests/test_batch.c | active |
| T021 | R115 | Verify `.rvlist`/`.m3u8` playlist parse and serialize round-trip | `make test` | tests/test_playlist.c | active |
| T022 | R108 | Verify PAL monotonic clock is positive, monotonic, and tracks elapsed time | `make test` | tests/test_pal_time.c | active |
| T023 | R112, R134 | Verify PAL directory enumeration, null-term invariant, sibling filtering/ordering | `make test` | tests/test_pal_fs.c | active |
| T024 | R109, R110 | Verify multi-page compositor: gutter placement, split halves, zoom/pan | `make test` | tests/test_compositor.c | active |
| T025 | R101, R102, R117, R147 | Windows canvas smoke check: window opens frameless, image decodes and renders, keys navigate, window size never changes on its own | `make win64` then run `dist/rubraview.exe <image>` on Windows | docs/tests/cases/T025-windows-smoke.md | manual |
| T026 | R113 | Verify non-destructive rotation/flip: corner mapping, axis swap, mirrored clockwise rule | `make test` | tests/test_transform.c | active |
| T027 | R120 | Verify click zones, wheel/modifier semantics, side buttons, swipe threshold | `make test` | tests/test_ui_input.c | active |
| T028 | R129, R130, R131 | Verify tile grid geometry, anchor clamping, detach/dock, pin, menu hierarchy and breadcrumb | `make test` | tests/test_ui_box.c | active |
| T029 | R116, R147 | Verify OSD fade and status line, hover titlebar timing and button hit zones, transitions, cursor auto-hide | `make test` | tests/test_ui_chrome.c | active |
| T030 | R133, R138 | Verify virtual scrolling, filmstrip budget interaction, picker breadcrumbs/type-ahead/multi-select | `make test` | tests/test_ui_browse.c | active |
| T031 | R116, R120, R129 | Windows reading-UI smoke check: floating boxes, OSD, hover titlebar, slide show, rotation, filmstrip | `make win64` then run `dist/rubraview.exe <image>` on Windows | docs/tests/cases/T031-windows-ui-smoke.md | manual |
| T032 | R120, R144 | Verify every §3.7.2 hotkey row in M3's scope resolves to its action, that the RFC's double-booked chords resolve in favour of the primary binding, and that §3.20's animation and sub-page rows resolve by context | `make test` | tests/test_default_keymap.c | active |
| T033 | R137 | Verify reading-position history parse/record/prune/round-trip and the portable vs AppData config hierarchy | `make test` | tests/test_history.c | active |
| T034 | R118, R136 | Verify CBZ and folder page sources, ComicInfo extraction, and consecutive archive traversal | `make test` | tests/test_pagesource.c | active |
| T035 | R138 | Verify the pre-cache ring window, fast-slideshow widening, budget eviction and worker submission | `make test` | tests/test_precache.c | active |
| T036 | R144 | Verify animation frame timing, speed ladder, stepping, kind classification and multi-page/ICO sub-page behaviour | `make test` | tests/test_animation.c | active |
| T038 | R118 | Verify CB7 reading on real 7-Zip archives: solid-block reuse, the §10.2 block-size guard, and refusal of encrypted, truncated and damaged archives | `make test` | tests/test_sevenzip.c | active |
| T039 | R127 | Verify the editing session: slider ranges, levels ordering, the tone curve staying a function, crop normalisation and aspect locks, resize lock, and a non-destructive commit | `make test` | tests/test_edit.c | active |
| T040 | R110 | Verify export settings clamping, format/extension agreement, and §3.10's re-encode-only-when-asked rule | `make test` | tests/test_export.c | active |
| T041 | R113 | Verify the `--batch` command line refuses typos and bad values, and that the engine filters, renames and resets its arena per file | `make test` | tests/test_batchrun.c | active |
| T042 | R150 | Verify parallel resampling is byte-identical to the single-threaded path across four filters and three sizes | `make test` | tests/test_resample_mt.c | active |
| T043 | R109 | Verify lossless JPEG rotation: four 90-degree turns return the original coefficients byte for byte, ragged images honour the edge policy, and a corrupt file errors instead of crashing | `make test` | tests/test_jpegtran.c | active |
| T044 | R127, R110, R113 | Verify the shared panel model: placement, row layout, hit testing, slider drag behaviour, stepping, toggles, choices and buttons | `make test` | tests/test_ui_panel.c | active |
| T045 | R127, R110, R113, R150 | Windows editing/export/batch check, including re-running T025's canvas steps against the rebuilt Direct2D 1.1 renderer | `make win64` then run `dist/rubraview.exe` on Windows | docs/tests/cases/T045-windows-edit-export-batch.md | manual |
| T046 | R139, R140, R141, R142 | Verify filename validation including the DOS device names, the undo stack's treatment of a permanent delete, curation folder mapping and mode, drop routing, single-instance hand-over and ProgID derivation | `make test` | tests/test_filemanage.c | active |
| T047 | R139, R140, R141, R142 | Windows triage, lifecycle and shell check — deletes and renames real files, and verifies unregistering leaves no registry key behind | `make win64` then run `dist/rubraview.exe` on Windows | docs/tests/cases/T047-windows-triage-shell.md | manual |
| T048 | R146 | Verify the settings schema: completeness, per-tab grouping, defaults inside their ranges, clamping of hand-edited files, round-trip preserving unknown keys, and keymap conflict detection within a context | `make test` | tests/test_settings.c | active |
| T049 | R146 | Verify §11.2's M9 criterion mechanically: every settings key the code reads is offered by a tab, and every schema row marked wired is really read | `sh scripts/project-check.sh` | scripts/check-settings.py | active |
| T050 | R146, R147 | Windows settings and packaging check: the tabs write `settings.ini`, portable mode keeps it beside the executable, and the packaged build runs on a machine with nothing beside it | `make package` then run `build/dist/rubraview.exe` on Windows | docs/tests/cases/T050-windows-settings-release.md | manual |
| T037 | R118, R145 | Windows archive smoke check: a real CBZ opens and pages turn, covers stand alone, colour-managed photos render, volumes chain | `make win64` then run `dist/rubraview.exe <file.cbz>` on Windows | docs/tests/cases/T037-windows-archive-smoke.md | manual |
