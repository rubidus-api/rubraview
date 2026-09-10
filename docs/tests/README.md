# Tests

TDD documentation and executable test organization.

Use `test-index.md` as the compact catalog. Put executable tests and test build files under `tests/`. `cases/` holds detail files only for tests that cannot state their contract in code (manual or device procedures, long fixtures); most rows have none.

Build output rules: compiled test binaries go to `build/tests/`; everything a build is meant to hand over goes to `dist/` — the executables at the top and the release bundle in `dist/rubraview-v<version>/` (owner, 2026-09-11: one place to look). Intermediate objects stay under `build/`. Do not compile test binaries into `tests/`.
