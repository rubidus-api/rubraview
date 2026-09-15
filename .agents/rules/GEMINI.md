# agy rules — rubraview

Project-specific facts for agy. The general operating policy lives in the
machine-global `~/.gemini/config/GEMINI.md`; do not restate it here.
Keep this file under 120 lines.

## Context economy

Read first: `CONTEXT.md`, `SPEC.md`, `include/rubraview/core.h`.
Never read whole: `vendor/proven/`, `build/`, `dist/`.
Search with `rg` rooted at `src/`, `include/`, `tests/`; do not scan the workspace.

## Tool discipline

- Build: `make` (Linux host test runner) or `make win64` (MinGW-w64 cross-build)
- Test (targeted): `make test`
- Test (full, slow): `make test`
- Gate: `scripts/gate.sh` runs the minimum tier for what changed; use it before reporting.
- Run builds and tests only through the commands above.
- Do not re-read a file already in context.

## Stop conditions

Stop and report when the same check fails three times for the same reason, or
when a step produces no new evidence.

## Effort and invocation

Default `--effort medium`. Raise only for subtle SIMD/color math, memory ownership, or MinGW cross-build debugging.
Headless (`--print`) auto-denies tools that need approval; for this project a
batch run needs interactive only.

## Project facts

- Language/build: Pure C23 (`-std=c23`), Makefile and nob.c
- Output goes to: `build/` (intermediate), `build/tests/` (test binaries), `dist/` (release executable)
- Cross-build or remote machine required: Windows binary cross-compiled via MinGW-w64 on a Linux build host
- Slow paths: FFmpeg dynamic loading, whole-directory batch processing
- Do not touch: `vendor/proven/` (vendored snapshot; report defects upstream)

## Prohibited

See `AGENTS.md` and `LESSONS.md` in this project. Do not restate their rules here;
read them when a decision depends on them.
