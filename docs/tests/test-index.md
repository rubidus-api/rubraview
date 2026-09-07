# Test Index

Short authoritative TDD catalog.

Read this before implementing or changing behavior. Open detailed case files only when relevant.

| ID | Requirement | Purpose | Command | Detail | Status |
|---|---|---|---|---|---|
| T000 | bootstrap | Confirm test catalog is initialized | manual review | docs/tests/cases/T000-bootstrap.md | active |
| T001 | R107 | Verify `rv_pixbuf_t` allocation, bounds, and arena ownership | `make test` | code contract | planned |
| T002 | R104 | Verify sRGB <-> Linear RGB LUT and color adjustments | `make test` | code contract | planned |
| T003 | R104 | Verify image resampling kernels (Bilinear, Bicubic, Lanczos) | `make test` | code contract | planned |
| T004 | R104 | Verify spatial convolution (Gaussian blur, unsharp mask) | `make test` | code contract | planned |
