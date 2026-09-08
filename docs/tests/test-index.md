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
