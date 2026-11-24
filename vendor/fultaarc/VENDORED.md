# Vendored FultaArc

- Upstream: `fultaarc_c_lib` (sibling checkout), MIT (see `LICENSE`); third-party parts in `THIRD_PARTY_NOTICES.md`
- Snapshot: commit `e1af87da` (tag `v0.1.4`), taken on 2026-11-24 with
  `git archive v0.1.4 LICENSE THIRD_PARTY_NOTICES.md include src vendor/lzma vendor/miniz vendor/bzip2 vendor/bzip2_alz vendor/zstd`
  (owner, 2026-11-24; D-393). Changes since `0dccf16` (`v0.1.1`..`v0.1.4`):
  - **v0.1.4** (`e1af87da`, 2026-11-24): RAR 1.5 password decryption (`src/rar/rar15_enc/rar15_cipher.c`, clean-room MIT, compiled by `Makefile` via `FULTA_OWN_SRCS`); memory-safety and security hardening (`d0d0664d`: `fa_wipe`/`proven_mem_wipe` on keys/passwords/plaintext, constant-time `fa_ct_eq` checks, `max_memory` bounds and overflow guards across ZIP/7z/RAR/ALZ/EGG/XZ/CAB/TAR/AR/CPIO/UDF/write, `gzip_probe` short-input guard, EGG OOM cleanup fix, zero-progress guards in bzip2/zstd, cancellation checks); performance improvements (8-block AES-NI CTR batching, static `fa_once` CRC tables, `proven_crc32_update` in RAR, `clz64` and run-copy fast paths in AZO, CAB hoisted folder buffer, O(1) ISO/UDF hash sets, 32 KiB UDF extraction buffer); `proven_c_lib v0.119.3` (D-048, D-049).
  - **v0.1.2 / v0.1.3** (2026-11-13): CAB LZX block look-ahead fix and modification times on RAR 2-5 & CAB (`FULTA_ARC_ENTRY_MTIME_LOCAL`); entry-name control characters sanitized to `_`; faster Deflate (~1.6x), LZH (~1.7x), AZO (~1.1x), and ALZ CRC-32; ZIP edit NTFS `0x000a` preservation.
  - Previous snapshot: `0dccf16` (2026-11-11, D-329). Since `24cd434` (v0.1.0, 2026-11-10, D-325): proven v0.113.0 with proven_crypto, `src/core/internal.h` including `proven/sha2.h` (asked by Rubraview, D-328), and a Zip64 end record with disk number 0xFFFFFFFF no longer read past its disk table (its D-043). Since `f41b3a0`, in v0.1.0: FultaArc's security review (its D-041)...
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
- Not taken: its tests, tools and documents, and its `vendor/proven` — FultaArc `v0.1.4` is built
  here on Rubraview's `vendor/proven` (`v0.119.3`, the same version as its own), with
  `-Ivendor/proven/crypto/include`, unchanged, its tests passing.
- Unmodified. Never edit it here; report defects upstream and resync.
