# Vendored: UnRAR 7.3.1's decompression and decryption, converted to C — the `unrar_proprietary` module

| | |
|---|---|
| Upstream | RARLAB UnRAR source, https://www.rarlab.com/rar_add.htm |
| Release | `unrarsrc-7.3.1.tar.gz` |
| SHA-256 of the release archive | `634900842a3737d9cc15bbcc71d4c74cc713437e0bca296a573424fe5f2660ab` |
| Retrieved | 2026-09-30 |
| Author | Alexander L. Roshal |
| Licence | UnRAR licence (freeware; extraction only, may not be used to make a RAR-compatible archiver) — `LICENSE.txt` here is the release's `license.txt`, unchanged |
| Modification | **converted from C++ to C** for this project (owner, 2026-09-30): `unpack.cpp`, `unpackinline.cpp`, `unpack15.cpp`, `unpack20.cpp`, `unpack30.cpp`, `unpack50.cpp`, `rarvm.cpp` and `getbits.*` into `rar_unpack.c`; single-threaded, one contiguous window, no fragmented window, no multithreading. 2026-10-01: `crypt.cpp`, `crypt3.cpp`, `crypt5.cpp`, `rijndael.cpp` (the generic tables only, no AES-NI / Neon), `sha1.cpp` and `sha256.cpp` into `rar_crypt.c` — decryption of RAR 3.x-5.0 archives (no RAR 1.5 / 2.0 encryption, no encryption at all). RAR 3.x PPMd is decoded by the LZMA SDK's public-domain `Ppmd7` with 7-Zip's `Ppmd7aDec.c` instead of UnRAR's `model.cpp`. |
| Module | 2026-10-01 (owner: "unrar 모듈은 분리하도록 해요. 라이선스가 다르니까요" / "파일이름은 unrar_proprietary"): this folder (was `vendor/unrar-c/`) builds `unrar_proprietary.dll` on its own (`make dist/unrar_proprietary.dll`); `unrar_proprietary.c` exports one function, `unrar_proprietary_api(version)`, returning the table `include/rubraview/rar_codec.h` describes. `rubraview.exe` contains none of this code and loads the DLL at run time from its own folder; any DLL of the same name exporting a table of the same version can take its place. |
| Ledger row | R007 |

The licence's condition — its second paragraph in the licence text, in the
documentation, and in the source comments — is met by `LICENSE.txt` here,
`THIRD_PARTY_NOTICES.md`, `licences/unrar_proprietary-LICENSE.txt` in the release zip,
the notices in the F1 help, and the headers of `rar_unpack.c` / `rar_unpack.h` / `rar_crypt.c` / `rar_crypt.h` / `unrar_proprietary.c` / `unrar_proprietary.h`.
