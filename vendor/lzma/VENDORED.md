# Vendored: LZMA SDK 24.08 (decode subset)

| | |
|---|---|
| Upstream | 7-Zip LZMA SDK, https://www.7-zip.org/sdk.html |
| Release | `lzma2408.7z` (24.08), retrieved from the project's SourceForge mirror |
| SHA-256 of the release archive | `105a12afcafcd5bdce70bc75e7f0e94eafd07293646278ea225e6601e048cf17` |
| Retrieved | 2026-09-09 |
| Author | Igor Pavlov (PPMd var.H: Dmitry Shkarin) |
| Licence | **Public domain** — verified by reading `DOC/lzma-sdk.txt` inside the retrieved archive; that text is copied here as `LICENSE.txt` |
| Modification | none — files are byte-identical to the release |
| Ledger row | R002 |

## Why it is here

CB7 (`.cb7`) support, RFC-0001 §3.8.2, owner decision D-3 of 2026-09-08.
7z is the only widely used comic-archive container this viewer supports
that is not ZIP, and its solid blocks are the reason §3.8.2 asks for a
*persistent* decoder rather than a per-file one.

Public domain places no condition on use or redistribution, so it does
not affect the project's own MIT licence. The notice is reproduced in
`THIRD_PARTY_NOTICES.md` because the authors deserve the credit, not
because a licence demands it.

## What was taken, and what was left

Only what decoding a 7z needs:

- container: `7z.h 7zArcIn.c 7zDec.c 7zBuf.c 7zBuf2.c 7zStream.c 7zAlloc.c 7zCrc.c 7zCrcOpt.c`
- decoders: `LzmaDec Lzma2Dec Ppmd7 Ppmd7Dec`
- filters: `Bcj2 Bra Bra86 BraIA64 Delta`
- support: `7zTypes.h 7zVersion.h 7zWindows.h CpuArch Compiler.h Precomp.h RotateDefs.h Sort`

Left out on purpose:

- **every encoder** — this program never writes an archive;
- **`7zFile.c`** — it opens files, and §3.8.1 forbids archive content
  from reaching the disk at all. `src/core/sevenzip.c` supplies a
  stream over the buffer that is already in memory instead;
- **`Aes*` / `Sha256*`** — leaving them out is what makes an encrypted
  archive come back as "unsupported coder" instead of a password prompt
  the viewer has no way to answer;
- **`Xz*`, `LzFind*`, `MtCoder`, `MtDec`, `Threads`, `DllSecur`** — not
  reachable from the 7z decode path. `Z7_ST` builds the single-threaded
  decoder because the viewer already owns a worker pool.

## Build

Compiled by the top-level `Makefile` into `build/lzma/` (host) and
`build/lzma-win/` (MinGW) with `-w`, because third-party code is not held
to this project's `-Werror -pedantic`. Sanitizers stay **on** for the
host build — this code parses untrusted input — with one exception,
`-fno-sanitize=alignment`: the SDK reads multi-byte fields straight out
of a byte buffer by design, and that check fires on every header.

## Updating

1. Fetch the new release, record its SHA-256 and the date above.
2. Copy the same file list; do not add encoders or `7zFile.c`.
3. Re-read `DOC/lzma-sdk.txt` and refresh `LICENSE.txt` — do not assume
   the terms carried over.
4. `make test` (T038 reads real archives) and `make win64`.
