# Vendored LZMA SDK (decoders, LZMA encoder)

- Upstream: 7-Zip LZMA SDK 24.08, https://www.7-zip.org/sdk.html, `lzma2408.7z`, SHA-256
  `105a12afcafcd5bdce70bc75e7f0e94eafd07293646278ea225e6601e048cf17`; `Ppmd7aDec.c` from 7-Zip 24.08's source
  (`7z2408-src.7z`, SHA-256 `4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`, `C/Ppmd7aDec.c`).
- Licence: public domain (Igor Pavlov; PPMd var.H by Dmitry Shkarin; `LICENSE.txt` is the SDK's statement).
- Origin: Rubraview's `vendor/lzma` (files byte-identical to the releases), imported unchanged 2026-10-02; FultaArc
  keeps the codec files only and drops the SDK's 7z container (`7zArcIn.c`, `7zDec.c`, `7zBuf*`, `7zStream.c`,
  `7z.h`, `7zAlloc.*`): FultaArc reads 7z with its own reader (D-006).
- Kept: `LzmaDec`, `Lzma2Dec`, `LzmaEnc` + `LzFind*` (7z writing), `Ppmd7`, `Ppmd7Dec` (7z PPMd), `Ppmd7aDec` (RAR 3
  PPMd), `Bra`, `Bra86`, `BraIA64`, `Bcj2`, `Delta` (7z filters), `7zCrc*`, `CpuArch`, `Sort` and headers.

## Added 2026-10-03: PPMd var.I (ZIP method 98)

`Ppmd8.c`, `Ppmd8.h`, `Ppmd8Dec.c`, unchanged from 7-Zip 24.08's source (`7z2408-src.7z`, SHA-256
`4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`, `C/`), public domain (Igor Pavlov; PPMd var.I by
Dmitry Shkarin). Used for ZIP method 98 (D-005 #3).

## Added 2026-10-11: PPMd var.I encoder (RFC-0004 phase 3)

`Ppmd8Enc.c`, unchanged from the same 7-Zip 24.08 source (`7z2408-src.7z`, SHA-256
`4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`, `C/Ppmd8Enc.c`), public domain. Compiled with
`-DZ7_ST`. Used for **writing** ZIP method 98 (`src/write/write.c` `zip_encode_body`): the writer mirrors 7-Zip's
per-level parameters (order = level+3, memory 1..32 MB, restore CUT_OFF at level ≥ 7) and emits the end marker the
genuine 7-Zip decoder reads to (verified with `7z t`). Picked up automatically by the Makefile's `vendor/lzma/*.c`
wildcard.

## Added 2026-10-11: LZMA2 encoder (RFC-0004 phase 4)

`Lzma2Enc.c`, `Lzma2Enc.h`, unchanged from the same 7-Zip 24.08 source (`7z2408-src.7z`, SHA-256
`4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`, `C/`), public domain. Compiled with `-DZ7_ST`
(single-threaded; no `MtCoder`/`MtDec`). Used for **writing** the 7z LZMA2 coder (id `0x21`) in
`src/write/write.c` `write_7z` (`Lzma2Enc_Encode2` over the solid input stream; the 1-byte dict property comes from
`Lzma2Enc_WriteProperties`). 7z BZip2 (id `04 02 02`) and Deflate (id `04 01 08`) reuse the already-vendored
libbzip2 and miniz; no new encoder files for those.

## Added 2026-10-11: PPMd var.H encoder (RFC-0004 phase 5)

`Ppmd7Enc.c`, unchanged from the same 7-Zip 24.08 source (`7z2408-src.7z`, SHA-256
`4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`, `C/Ppmd7Enc.c`), public domain. Compiled with
`-DZ7_ST`. Used for **writing** the 7z PPMd coder (id `0x030401`) in `src/write/write.c` `write_7z`: the `Ppmd7z_*`
range encoder (the 7z variant, matching the reader's `fa_dec_ppmd7z` — **not** the `Ppmd7a` RAR variant), 5-byte
property `[order, memSize LE32]`, parameters modelled on 7-Zip's levels (order 4..32, memory 2^20..2^25). 7z PPMd
is size-bounded, so no end marker is written (unlike ZIP PPMd var.I). Picked up by the `vendor/lzma/*.c` wildcard.

## Added 2026-10-11: BCJ2 encoder (RFC-0004 phase 7)

`Bcj2Enc.c`, unchanged from the LZMA SDK 24.08 (`lzma2408.7z`, SHA-256
`105a12afcafcd5bdce70bc75e7f0e94eafd07293646278ea225e6601e048cf17`, `C/Bcj2Enc.c`, SHA-256
`aec561859ae273c983784f6edc98eacd315398e0cec68411cd906198d7237d0f`), public domain — the same SDK the already-vendored
`Bcj2.c`/`Bcj2.h` come from, and byte-identical to `C/Bcj2Enc.c` in `7z2408-src.7z`
(`4df7a62e5ce503892f500b1f96f0a954931c5266900c439102040957b25a90c6`). Compiled with `-DZ7_ST`. Used for **writing**
the 7z BCJ2 filter (coder id `0x0303011B`, 4 input streams, 1 output) in `src/write/write.c` `write_7z`: the x86
branch splitter producing the MAIN/CALL/JUMP/RC streams, mirroring the reader's `fa_dec_bcj2` input order
(0=main, 1=call, 2=jump, 3=rc). Picked up by the `vendor/lzma/*.c` wildcard.
