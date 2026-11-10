# Vendored FultaArc

- Upstream: `fultaarc_c_lib` (sibling checkout), MIT (see `LICENSE`); third-party parts in `THIRD_PARTY_NOTICES.md`
- Snapshot: tag `v0.1.0`, commit `24cd434`, taken on 2026-11-10 with
  `git archive 24cd434 LICENSE THIRD_PARTY_NOTICES.md include src vendor/lzma vendor/miniz vendor/bzip2 vendor/bzip2_alz vendor/zstd`
  (owner, 2026-11-10: "fultaarc 의 재벤더링 요청합니다"; D-325). Since `f41b3a0`: FultaArc's security review (its
  D-041): double frees and a heap over-read fixed, header injection and unsafe names refused (an entry's name never
  holds a drive or a ".." part as Windows reads one, "_" when nothing safe remains), one memory budget with a RAR
  window and an ALZ entry checked against it before they are allocated, 7z folders grown as parsed, exclusive and
  random temporaries, the first-use tables built once across threads (`src/core/fa_once.h`), and CBMC proofs of the
  core input handlers; the API unchanged. Before it, `f41b3a0` (2026-11-10; RV-207: the edit and convert routes write encrypted ZIP and 7z, streamed; a zero-size file no longer freed twice;
  TAR and its gzip / bzip2 / xz / zstd / compress / lzip wrappers, ar, cpio, ISO 9660, UDF, CAB, LHA and RAR 1.4-1.5
  read besides); D-263, asked by Rubraview (RV-219): the ZIP edit and convert routes take a codec (Store, Deflate,
  BZip2, LZMA, PPMd, Zstandard), split output (`volume_size`: 7z `x.7z.001...`, ZIP `x.z01...x.zip`, all volumes
  renamed only on success, `FULTA_ARC_ERR_EXISTS` when an older set's later volume is in the way) and a 7z's header
  encrypted with its password (`encrypt_names`); D-265: the LZMA SDK 26.05 (7-Zip's dictionaries per level),
  ARM-Thumb and RISC-V filters, every allocation through proven's allocator (`fulta_arc_free` for
  `fulta_arc_extract_alloc`, which Rubraview does not call), Deflate decoding faster; D-040, asked by Rubraview
  (D-319): `FULTA_ARC_ENTRY_MTIME_LOCAL` on a time read from a local-time field (ZIP's DOS time with no UTC field,
  ALZ, RAR 1.5, EGG, LHA level 0/1; RAR 2-4, CAB and ARJ give no times), NTFS `0x000a` read, a ZIP's DOS field
  written in local time (`fulta_arc_write_options_t.dos_time`, 0 = the machine's offset)). Before it: `ad1a532`
  (2026-11-03), `1bc526c`
  (2026-11-02), `e620e6f` (2026-10-31), `4fb7c06` (2026-10-10),
  `e3dcb85` (2026-10-08, without ALZ and EGG).
- What it is here for: every archive Rubraview reads (ZIP, 7z, RAR 2.0-7.0, ALZ, EGG) and writes or edits
  (ZIP, 7z), through `include/fulta/arc.h` (owner, 2026-10-08; DECISIONS D-74, D-85).
- Built as FultaArc's own Makefile builds it: `vendor/bzip2_alz` is ALZ's altered bzip2 decoder, compiled with
  `-include vendor/bzip2_alz/alz_bzip2_rename.h` so that it stands beside the ordinary one; `src/lha/lhadec/src` with
  `-include src/lha/lhadec_shim.h`; Zstandard is the whole `zstd.c` (FultaArc reads `.zst` and writes none).
- Not taken: its tests, tools and documents, and its `vendor/proven` — v0.112.3; FultaArc is built
  here on Rubraview's `vendor/proven` (v0.112.4 since D-320), unchanged, its tests passing.
- Unmodified. Never edit it here; report defects upstream and resync.
