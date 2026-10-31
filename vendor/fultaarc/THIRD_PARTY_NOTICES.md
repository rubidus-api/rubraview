# Third-party notices

FultaArc is MIT-licensed (`LICENSE`). It contains or derives data from the following works.

| Component | Where | Licence |
|---|---|---|
| proven_c_lib v0.6.0 | `vendor/proven/` | MIT (`vendor/proven/LICENSE`) |
| LZMA SDK 24.08 (Igor Pavlov; PPMd var.H by Dmitry Shkarin) | `vendor/lzma/` | public domain (`vendor/lzma/LICENSE.txt`) |
| miniz 3.1.2 | `vendor/miniz/` | MIT (`vendor/miniz/LICENSE`) |
| bzip2 1.0.8 (Julian Seward) | `vendor/bzip2/`, `vendor/bzip2_alz/` (altered, marked) | bzip2 licence (`vendor/bzip2/LICENSE`) |
| Zstandard 1.5.7 decoder (Meta Platforms, Inc. and affiliates) | `vendor/zstd/` | BSD-3-Clause (`vendor/zstd/LICENSE`; upstream is dual BSD/GPLv2) |
| Unicode mapping data (Microsoft code pages 437, 866, 1251, 1252, 932, 936, 949, 950; KOI8-R) | `src/text/cp_tables.c` (generated) | Unicode License v3, below |

## Consulted, not contained

These works were used to *make* part of FultaArc but no code, table or text of theirs is in it, so they carry no
notice obligation here; they are named so that a reader checking FultaArc's licence position can see them. How each was used, and on what
terms, is set out per format in `docs/provenance/`.

| Work | Licence | How it was used |
|---|---|---|
| Haruhiko Okumura, `ar002` (1990) | public domain (the author's `00README.txt`: "released to the public domain") | read, built and run by the LHA clean room as a reference; `src/lha/lhadec/` re-expresses the format in that room's own specification and code |
| Simon Howard, lhasa 0.6.0 | ISC | its `lha` program run as a black box (output compared) and its test archives used as test data, by the LHA clean room and by FultaArc's tests (`tests/manifests/lha.txt`); its source was not read |
| UNLHA32.DLL 3.00 (Micco; compression routines by Haruyasu Yoshizaki and Haruhiko Okumura) | freeware, under its package's terms | run as a black box by the LHA clean room, at the project owner's instruction, to observe `-lh2-`/`-lh3-`; not redistributed; its source was not read |
| Yoshihiko Mino, PMarc 2.00 and PMext 2.22 | freeware, no use for profit | run privately as a black box by the LHA clean room to observe `-pm2-`; not redistributed; no PMarc source or format description was read |
| RunCPM (Mockba the Borg) | MIT | built and run by the LHA clean room as the CP/M runtime for the PMarc programs; nothing of it is in FultaArc |
| RAR 1.55 for DOS | shareware for evaluation (as recorded when the room was set up; the licence screen was not kept) | run by the RAR 1.5 clean room as a black box (archives written from the room's inputs, and extracted); its `RAR.DOC` read for commands and switches only; never disassembled or inspected; not redistributed. Its output from the room's inputs is FultaArc's private test data (`tests/manifests/rar15.txt`) |
| RAR 1.40.2 beta for DOS | no licence text in the bundle at hand | run the same way, for the older RAR 1.4 format; `RAR.DOC` read as a manual; not redistributed |
| RAR 2.50 / 2.02 / 2.00 for DOS | not recorded | run by the RAR 1.5 clean room only to test archives (an Ok/Failed verdict, nothing else); not redistributed |
| DOSBox 0.74-3, SDL | GPL-2 / LGPL-2+ | run by the RAR 1.5 clean room to execute the DOS programs; nothing of them is in FultaArc |

`src/lha/lhadec/` itself is FultaArc's own code under MIT: `src/lha/lhadec/LICENSE`, the clean room's copy, is the
MIT text of `LICENSE` with the same copyright line, without `LICENSE`'s paragraph on vendored components.

`src/rar/rar15/` is likewise FultaArc's own code under MIT: its files carry the clean room's SPDX header and MIT
text (`unpack15.h`).

If FultaArc ever publishes its LHA test archives (the lhasa corpus, kept in the private fixtures), lhasa's ISC
copyright and permission notice must go with them.

## Unicode License v3

UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY DOWNLOADING, INSTALLING, COPYING OR OTHERWISE
USING DATA FILES, AND/OR SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE TERMS AND
CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES
OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a copy of data files and any associated
documentation (the "Data Files") or software and any associated documentation (the "Software") to deal in the Data
Files or Software without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, and/or sell copies of the Data Files or Software, and to permit persons to whom the Data Files
or Software are furnished to do so, provided that either (a) this copyright and permission notice appear with all
copies of the Data Files or Software, or (b) this copyright and permission notice appear in associated
Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL
INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not be used in advertising or otherwise
to promote the sale, use or other dealings in these Data Files or Software without prior written authorization of
the copyright holder.
