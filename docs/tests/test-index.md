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
