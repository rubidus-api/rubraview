# Vendored FultaArc

- Upstream: `fultaarc_c_lib` (sibling checkout), MIT (see `LICENSE`); third-party parts in `THIRD_PARTY_NOTICES.md`
- Snapshot: commit `1bc526c` (version 0.1.0, no tag yet), taken on 2026-11-02 with
  `git archive 1bc526c LICENSE THIRD_PARTY_NOTICES.md include src vendor/lzma vendor/miniz vendor/bzip2 vendor/bzip2_alz vendor/zstd`
  (RV-207: the edit and convert routes write encrypted ZIP and 7z, streamed; a zero-size file no longer freed twice;
  TAR and its gzip / bzip2 / xz / zstd / compress / lzip wrappers, ar, cpio, ISO 9660, UDF, CAB, LHA and RAR 1.4-1.5
  read besides); D-263, asked by Rubraview (RV-219): the ZIP edit and convert routes take a codec (Store, Deflate,
  BZip2, LZMA, PPMd, Zstandard), split output (`volume_size`: 7z `x.7z.001...`, ZIP `x.z01...x.zip`, all volumes
  renamed only on success, `FULTA_ARC_ERR_EXISTS` when an older set's later volume is in the way) and a 7z's header
  encrypted with its password (`encrypt_names`). Before it: `e620e6f` (2026-10-31), `4fb7c06` (2026-10-10),
  `e3dcb85` (2026-10-08, without ALZ and EGG).
- What it is here for: every archive Rubraview reads (ZIP, 7z, RAR 2.0-7.0, ALZ, EGG) and writes or edits
  (ZIP, 7z), through `include/fulta/arc.h` (owner, 2026-10-08; DECISIONS D-74, D-85).
- Built as FultaArc's own Makefile builds it: `vendor/bzip2_alz` is ALZ's altered bzip2 decoder, compiled with
  `-include vendor/bzip2_alz/alz_bzip2_rename.h` so that it stands beside the ordinary one; `src/lha/lhadec/src` with
  `-include src/lha/lhadec_shim.h`; Zstandard is the whole `zstd.c` (FultaArc reads `.zst` and writes none).
- Not taken: its tests, tools and documents, and its `vendor/proven` (v0.6.0 at this snapshot) — FultaArc is built
  here on Rubraview's `vendor/proven` (v0.76.0, `vendor/proven/VENDORED.md`), which builds and passes this project's
  tests with it (RV-208: the note said `981911b` before). FultaArc is moving to proven v0.107.x next; resync then.
- Unmodified. Never edit it here; report defects upstream and resync.
