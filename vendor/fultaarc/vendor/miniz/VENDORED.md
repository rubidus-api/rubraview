# Vendored miniz

This directory is a pinned snapshot of miniz, taken from the upstream
release archive rather than from a branch, so the bytes are reproducible.

- Upstream: https://github.com/richgel999/miniz
- Release: `3.1.2`
- Archive: `miniz-3.1.2.zip`, SHA-256
  `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`
- Retrieved: 2026-09-09
- Licence: MIT (see `LICENSE`, copied verbatim from the archive)

Copied from the archive unmodified: `miniz.c`, `miniz.h`, `LICENSE`,
`readme.md`. The examples were not copied.

## Why it is here

Owner decision D-2 (2026-09-08, `DECISIONS.md`): CBZ pages are DEFLATE
compressed, `proven_c_lib` has no inflater, and writing one that stands
up to hostile archives (RFC-0001 §10.2) needs a fuzzing history this
project does not have. miniz has one.

## How it is used

Only through `src/core/archive.c`, and only for decompression — nothing
else in the tree includes `miniz.h`, which is the "one job, one module"
rule RFC-0001 §8.1 sets for a vendored library. `MINIZ_NO_STDIO`,
`MINIZ_NO_ARCHIVE_APIS` and friends are defined at the include site so
the ZIP reader stays rubraview's own: miniz supplies the inflate
algorithm, not the container parsing, which §3.8.1's O(1) central
directory index already does.

## Updating

Do not edit the files here. To move to a newer release, replace them
from that release's archive and update the version, hash and date above.
