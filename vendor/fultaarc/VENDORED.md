# Vendored FultaArc

- Upstream: `fultaarc_c_lib` (sibling checkout), MIT (see `LICENSE`); third-party parts in `THIRD_PARTY_NOTICES.md`
- Snapshot: commit `4fb7c06` (version 0.1.0, no tag yet), taken on 2026-10-10 with
  `git archive 4fb7c06 LICENSE THIRD_PARTY_NOTICES.md include src/core src/codec src/crypto src/text src/platform src/zip src/7z src/write src/rar src/alz src/egg src/azo vendor/lzma vendor/miniz vendor/bzip2 vendor/bzip2_alz vendor/zstd`
  (before it: `e3dcb85`, 2026-10-08, without ALZ and EGG).
- What it is here for: every archive Rubraview reads (ZIP, 7z, RAR 2.0-7.0, ALZ, EGG) and writes or edits
  (ZIP, 7z), through `include/fulta/arc.h` (owner, 2026-10-08; DECISIONS D-74, D-85).
- Built as FultaArc's own Makefile builds it: `vendor/bzip2_alz` is ALZ's altered bzip2 decoder, compiled with
  `-include vendor/bzip2_alz/alz_bzip2_rename.h` so that it stands beside the ordinary one.
- Not taken: its tests, tools and documents, and its `vendor/proven` — FultaArc is built on Rubraview's
  `vendor/proven`, which is the same snapshot (`981911b`).
- Unmodified. Never edit it here; report defects upstream and resync.
