# Vendored FultaArc

- Upstream: `fultaarc_c_lib` (sibling checkout), MIT (see `LICENSE`); third-party parts in `THIRD_PARTY_NOTICES.md`
- Snapshot: commit `e3dcb85` (version 0.1.0, no tag yet), taken on 2026-10-08 with
  `git archive e3dcb85 LICENSE THIRD_PARTY_NOTICES.md include src/core src/codec src/crypto src/text src/platform src/zip src/7z src/write src/rar vendor/lzma vendor/miniz vendor/bzip2 vendor/zstd`
- What it is here for: every archive Rubraview reads (ZIP, 7z, RAR 2.0-7.0) and writes (ZIP, 7z), through
  `include/fulta/arc.h` (owner, 2026-10-08; DECISIONS D-74).
- Not taken: FultaArc's `src/alz`, `src/egg`, `src/azo` and `vendor/bzip2_alz` (held out of its build until their
  clean rooms deliver, FultaArc D-014), its tests, tools and documents, and its `vendor/proven` — FultaArc is built
  on Rubraview's `vendor/proven`, which is the same snapshot (`981911b`).
- Unmodified. Never edit it here; report defects upstream and resync.
