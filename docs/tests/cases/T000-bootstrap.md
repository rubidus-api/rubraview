# T000: Bootstrap Test Catalog

## Requirement

The project has an initialized TDD catalog.

## Verification Method

Manual review.

## Assertions

- `docs/tests/test-index.md` exists.
- Future behavior changes add or update TDD entries before implementation.

## Checked, 2026-09-24

- `docs/tests/test-index.md` exists and lists every case; the case files
  beside it match the rows.
- The practice the second assertion asks for is visible in the log: the
  features added this month each landed with a test that runs — the help
  window with `test_help`, the DVD subtitles with `test_vobsub`, picking
  files with `test_ui_browse`, the sibling path with `test_path`, the
  multi-language subtitles with `test_subtitle` — and the manual cases
  (T077-T080) were written with the work, not after it.
- This case is a standing check on how the project works, not on the
  program; there is nothing on a Windows machine for it.
