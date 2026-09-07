# Tests

TDD documentation and executable test organization.

Use `test-index.md` as the compact catalog. Put executable tests and test build files under `tests/`. `cases/` holds detail files only for tests that cannot state their contract in code (manual or device procedures, long fixtures); most rows have none.

Build output rules: compiled test binaries go to `build/tests/`; release packaging staging to `build/dist/`; final distribution artifacts only to `dist/`. Do not compile test binaries into `tests/`.
