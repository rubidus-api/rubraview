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
