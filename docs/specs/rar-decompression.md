<!-- Spec: RAR archive reading and decompression, for implementers. Written from libarchive 3.7.7
(archive_read_support_format_rar.c, archive_read_support_format_rar5.c; BSD-2), nwaples/rardecode
(BSD-2), RARLAB's "RAR 5.0 archive format" technote, the LZMA SDK's Ppmd7.h (public domain); standards
referred to: FIPS 197, FIPS 180-4, RFC 2104, RFC 8018, RFC 7693 and the BLAKE2 paper, ISO 3309 CRC-32. -->

# RAR archives: headers, decompression, encryption (a specification for implementers)

Written 2026-10-01 for Rubraview in the `rar-spec` clean-room session (`docs/cleanroom/README.md`). It describes
RAR 4 (archive format 1.5, written by RAR 2.9 to 6.x with `-ma4`) and RAR 5 archives in prose, tables and
pseudo-code, so that a decoder can be written from this document alone. No code was copied into it. Every
section names where its content comes from.

## Sources

| Short name | What | Licence | Read for |
|---|---|---|---|
| **LA4** | libarchive 3.7.7, `libarchive/archive_read_support_format_rar.c` | BSD-2 | RAR 4 headers, RAR 2.9 LZ, PPMd glue, RAR 3 standard filters |
| **LA5** | libarchive 3.7.7, `libarchive/archive_read_support_format_rar5.c` | BSD-2 | RAR 5 headers, RAR 5 LZ and filters, BLAKE2sp use, multivolume blocks |
| **RD** | nwaples/rardecode (Go, `master` as of 2026-09-26, commit `fa6448a`), all `*.go` files | BSD-2 | RAR 2.0 (LZ and audio), RAR 3 VM and Itanium filter, encryption (RAR 3 and RAR 5), MAC'd checksums, solid streams, volume naming |
| **TN** | RARLAB, "RAR 5.0 archive format", https://www.rarlab.com/technote.htm (fetched 2026-10-01) | published description | RAR 5 header layout, flags and records |
| **PPMD** | LZMA SDK `C/Ppmd7.h` (2023-04-02, public domain) | public domain | names of the PPMd var.H model entry points |

Source functions are written as `LA4:parse_codes`, `RD:decode29_lz.go:decodeOffset` and so on. Coverage gaps
of the sources, which matter when they disagree or only one speaks:

- LA4 does **not** decode solid RAR 4 archives, encrypted RAR 4 data or headers, RAR 2.0 (unpack version 20/26)
  data, the Itanium filter, PPMd-embedded filters or non-standard RAR 3 VM programs. Those parts come from RD alone.
- LA5 does not decrypt; RD does. LA5 checks BLAKE2sp; RD does not.
- RAR 1.5 compression (unpack version 15) and the pre-RAR-3 ciphers are in neither source and are **out of
  scope**: report such entries as unsupported.

Section 15 lists every point where the sources disagree or only one source speaks, with the fixture that
decides it. Facts checked against the fixtures in `tests/fixtures/rar/` during this session (header layouts, the
key derivations, the MAC'd CRC, the split-part checksum rule, BLAKE2sp) say so where they are given.

## Conventions

- All multi-byte integers in headers are **little-endian** unless stated otherwise.
- Bit numbers count from 0 = least significant. Flag values are written in hex.
- **Bit streams** (compressed data in every RAR version) are read **most-significant bit first**: the first bit of
  the stream is bit 7 of the first byte. "Read n bits" yields an n-bit unsigned number whose first-read bit is its
  most significant. (LA4:`rar_br_bits`, LA5:`read_bits_16`, RD:`bit_reader.go:readBits`.)
- "Distance" d means "the byte d positions before the current output position" (d = 1 is the previous byte).
- `vint` is the RAR 5 variable-length integer (section 3.1). `u8/u16/u32/u64` are little-endian unsigned.
- Pseudo-code is language-neutral; `>>`, `<<`, `&`, `|`, `^` are bit operations on unsigned integers of the
  stated width; "mod 2^32" is spelt out where wrap-around matters.

---

## 1. Recognising an archive

Source: TN "RAR 5.0 signature", "Self-extracting module"; LA4:`archive_read_format_rar_bid`, `skip_sfx`;
LA5:`bid_standard`, `bid_sfx`, `try_skip_sfx`; RD:`bufio.go` (format version from the signature).

| Signature bytes | Format |
|---|---|
| `52 61 72 21 1A 07 00` ("Rar!" 1A 07 00), 7 bytes | RAR 4 (archive format 1.5; called RAR 4 here) |
| `52 61 72 21 1A 07 01 00`, 8 bytes | RAR 5 |

The signature is normally at offset 0. A self-extracting archive (SFX) has an executable stub in front (it begins
with `MZ` or `7F 45 4C 46`). TN says to search "from beginning and up to maximum SFX module size", currently 1 MB.
LA4 searches the first 128 KiB and LA5 the first 512 KiB, both only at offsets that are multiples of 16; a decoder
should search every offset up to 1 MiB. Everything before the signature is ignored.

Fixtures: every `*.rar` (signature at offset 0). No SFX fixture exists.

---

## 2. RAR 4 headers

Sources: LA4:`archive_read_format_rar_read_header`, `read_header`, `read_exttime`, `get_time`;
RD:`archive15.go` (`readBlockHeader`, `parseFileHeader`, `parseArcBlock`, `decodeName`, `readExtTimes`,
`nextBlock`). Layouts below were checked against all RAR 4 fixtures.

### 2.1 Block header

After the 7-byte signature the archive is a sequence of blocks. Every block starts with this 7-byte header:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | HEAD_CRC: low 16 bits of the CRC-32 (section 5.1) of the header bytes from offset 2 to HEAD_SIZE-1 |
| 2 | u8 | HEAD_TYPE |
| 3 | u16 | HEAD_FLAGS |
| 5 | u16 | HEAD_SIZE: size of the whole header including these 7 bytes |

Common flags: `0x8000` (LONG_BLOCK / ADD_SIZE present): a u32 ADD_SIZE follows at offset 7 and that many bytes of
data follow the header. `0x4000` appears on end-of-archive headers in the fixtures (LA4 names it
HD_MARKDELETION); ignore it.

The block occupies HEAD_SIZE bytes, then its data area (ADD_SIZE bytes, or PACK_SIZE for file blocks). To skip a
block of an unknown or uninteresting type: skip HEAD_SIZE, then skip ADD_SIZE if `0x8000` is set. HEAD_SIZE < 7
is a corrupt archive.

Block types:

| Type | Name | Handling |
|---|---|---|
| 0x72 | MARK_HEAD | the signature itself (`52 61 72 21 1A 07 00` read as a block header has type 0x72); skip 7 bytes |
| 0x73 | MAIN_HEAD | archive header, section 2.2 |
| 0x74 | FILE_HEAD | file header, section 2.3 |
| 0x75 | COMM_HEAD | old archive comment; skip (see 2.2 for the embedded form) |
| 0x76 | AV_HEAD | authenticity information; skip |
| 0x77 | SUB_HEAD | old sub-block; skip |
| 0x78 | PROTECT_HEAD | recovery record; skip |
| 0x79 | SIGN_HEAD | signature; skip |
| 0x7A | NEWSUB_HEAD | service header (layout of a file header; names such as `CMT`, `ACL`, `STM`, `RR`); skip its data |
| 0x7B | ENDARC_HEAD | end of archive, section 2.6 |

Any other type: LA4 treats it as a corrupt archive; RD skips it using the rule above. Prefer skipping.

### 2.2 Main archive header (0x73)

| Offset | Size | Field |
|---|---|---|
| 0 | 7 | block header (HEAD_SIZE normally 13) |
| 7 | u16 | reserved (LA4 `reserved1`) |
| 9 | u32 | reserved (LA4 `reserved2`) |
| 13 | u8 | encryption version; present only when flag `0x0200` is set |

Flags (LA4 `MHD_*`, RD `arc*`):

| Flag | Meaning |
|---|---|
| 0x0001 | volume: the archive is one part of a multi-volume set |
| 0x0002 | archive comment present (old style, see below) |
| 0x0004 | locked |
| 0x0008 | solid archive |
| 0x0010 | new volume naming (`name.partNN.rar`); clear = old naming (`name.rar`, `.r00`, ...) |
| 0x0020 | authenticity information present |
| 0x0040 | recovery record present |
| 0x0080 | **block headers are encrypted** (section 6.1.3) |
| 0x0100 | first volume |
| 0x0200 | encryption-version byte present |

Old-style comment (RD:`readBlockHeader`, single source): when the main header has flag `0x0002`, treat it as
13 bytes long whatever HEAD_SIZE says; the comment block (type 0x75) follows at offset 13 and is read as the next
block. The CRC of a 0x75 block covers only its bytes 2..12 (11 bytes), not the whole header.

Fixtures: main flags 0x0000 (most), 0x0008 (`rar4_enc_solid`), 0x0080 (`rar4_enc_headers`,
`rar_encryption_header`), 0x0101 / 0x0001 (`rar4_oldvol.*`), 0x0111 / 0x0011 (`rar4_vol.part*`), 0x0199 / 0x0099
(`rar4_vol_enc.part*`).

### 2.3 File header (0x74) and service header (0x7A)

| Offset | Size | Field |
|---|---|---|
| 0 | 7 | block header |
| 7 | u32 | PACK_SIZE: low 32 bits of the packed data size (the data area that follows the header) |
| 11 | u32 | UNP_SIZE: low 32 bits of the unpacked size |
| 15 | u8 | HOST_OS: 0 MS-DOS, 1 OS/2, 2 Win32, 3 Unix, 4 Mac OS, 5 BeOS |
| 16 | u32 | FILE_CRC: CRC-32 of the unpacked data (but see 4.3 for split files) |
| 20 | u32 | FTIME: modification time, MS-DOS format (2.5) |
| 24 | u8 | UNP_VER: version needed to unpack, selects the algorithm (2.4) |
| 25 | u8 | METHOD: 0x30 stored, 0x31..0x35 compressed (fastest .. best) |
| 26 | u16 | NAME_SIZE |
| 28 | u32 | ATTR: file attributes (2.5) |
| 32 | u32 | HIGH_PACK_SIZE: only if flag `0x0100` |
| 36 | u32 | HIGH_UNP_SIZE: only if flag `0x0100` |
| ... | NAME_SIZE | FILE_NAME (2.4) |
| ... | 8 | SALT: only if flag `0x0400` (section 6.1) |
| ... | var | EXT_TIME: only if flag `0x1000` (2.5) |

Anything after these fields up to HEAD_SIZE is to be ignored. The data area of PACK_SIZE bytes follows the header.
With flag `0x0100` the sizes are `PACK_SIZE | HIGH_PACK_SIZE << 32` and `UNP_SIZE | HIGH_UNP_SIZE << 32`.
The unpacked size is **unknown** when it is all ones: 0xFFFFFFFF without `0x0100`, or 2^64-1 with it
(RD:`parseFileHeader`); then decode until the stream signals end of file.

File flags (LA4 `FHD_*`, RD `file*`):

| Flag | Meaning |
|---|---|
| 0x0001 | SPLIT_BEFORE: data continues from the previous volume (4.1) |
| 0x0002 | SPLIT_AFTER: data continues in the next volume |
| 0x0004 | PASSWORD: data is encrypted (6.1) |
| 0x0008 | old-style file comment present |
| 0x0010 | SOLID: this file continues the solid stream of the previous file (7.5) |
| 0x00E0 | dictionary field, 3 bits: value n = (flags >> 5) & 7. n = 0..6: dictionary 64 KiB << n (64 KiB .. 4 MiB); n = 7: **directory** |
| 0x0100 | LARGE: HIGH_PACK_SIZE and HIGH_UNP_SIZE present |
| 0x0200 | UNICODE: file name has a Unicode part (2.4) |
| 0x0400 | SALT present |
| 0x0800 | VERSION: name carries a version suffix `;n` |
| 0x1000 | EXT_TIME present |
| 0x2000 | EXTFLAGS (meaning not given by the sources; ignore) |
| 0x8000 | always set on file headers (data area follows) |

Negative sizes (bit 63 set, other than the "unknown" value) are a corrupt header (LA4:`read_header`).

Service headers (0x7A) use exactly this layout; their name (`CMT` comment, `ACL`, `STM` stream, `RR` recovery
record, ...) says what the data is. A reader that only extracts files skips them (header, then PACK_SIZE bytes).
The comment data may be compressed (`rar_subblock.rar` has a `CMT` block with METHOD 0x33).

### 2.4 Algorithm selection, names

**UNP_VER** (RD:`parseFileHeader`; LA4 ignores this field and always uses the 2.9 decoder):

| UNP_VER | Algorithm |
|---|---|
| any, with METHOD 0x30 | stored: data area is the file (after decryption if encrypted) |
| 15 | RAR 1.5 algorithm: not covered (unsupported) |
| 20, 26 | RAR 2.0 algorithm, section 8 |
| 29 | RAR 2.9/3.x algorithm (LZ + PPMd + filters), sections 9 to 11 |
| other | unknown: unsupported |

Fixtures use UNP_VER 20 (stored files and directories only) and 29. `rar_ppmd_use_after_free.rar` (a fuzzing
case) has 13.

**FILE_NAME** (LA4:`read_header`, RD:`decodeName`):

- Flag `0x0200` clear: the name is in the archiver's OEM/ANSI code page (not recorded; `rar_unicode.rar` shows
  Shift-JIS bytes). RD treats it as UTF-8; LA4 uses the current locale. Pick a configurable code page.
- Flag `0x0200` set and the name contains no zero byte: the name is UTF-8.
- Flag `0x0200` set and the name contains a zero byte: the bytes before the zero are a narrow name (in files
  written by RAR 6.24, UTF-8; older files, an OEM code page) and the bytes after it encode the UTF-16 name:

```
narrow = bytes before the 0; enc = bytes after the 0; out = []        # UTF-16 code units
high = enc[0]; pos = 1; flagbits = 0
while pos < len(enc) and len(out) < len(narrow):           # see section 15 on the stop condition
    if flagbits == 0: flags = enc[pos]; pos += 1; flagbits = 8
        (stop if pos == len(enc))
    flagbits -= 2; mode = (flags >> flagbits) & 3          # 2-bit modes, most significant pair first
    mode 0: out += enc[pos];                 pos += 1      # high byte 0
    mode 1: out += (high << 8) | enc[pos];   pos += 1
    mode 2: out += enc[pos] | enc[pos+1] << 8; pos += 2    # stop if fewer than 2 bytes remain
    mode 3: L = enc[pos]; pos += 1
            n = (L & 0x7F) + 2
            if L & 0x80: corr = enc[pos]; pos += 1
                         repeat n times: out += (high << 8) | ((narrow[len(out)] + corr) & 0xFF)
            else:        repeat n times: out += narrow[len(out)]
```

Mode 3 copies from the narrow name at the **same character index** as the output. Then convert `out` from UTF-16
(surrogate pairs allowed) to the reader's string form. Checked on `rar4_enc_data.rar` (`sub\` copied by mode 3,
then U+D55C by mode 1, U+AE00 by mode 2, ...).

In every case replace `\` with `/` (RAR 4 writes `\` as the separator). With flag `0x0800` the name ends in `;n`
(a decimal file version); RD strips it into a version number.

### 2.5 Times and attributes

**MS-DOS time** (FTIME and the times in EXT_TIME; LA4:`get_time`, RD:`parseDosTime`), local time:

| Bits | Field |
|---|---|
| 0-4 | seconds / 2 |
| 5-10 | minutes |
| 11-15 | hours |
| 16-20 | day (1-31) |
| 21-24 | month (1-12) |
| 25-31 | year - 1980 |

**EXT_TIME** (LA4:`read_exttime`, RD:`readExtTimes`): a u16 FLAGS, then for each of four times in this order —
mtime (FLAGS bits 12-15), ctime (bits 8-11), atime (bits 4-7), archive time (bits 0-3) — a 4-bit field r:

- r & 8 clear: this time is absent; nothing is stored for it.
- otherwise: for mtime the base is FTIME (nothing stored); for the other three a u32 MS-DOS time is stored first.
- r & 4: add one second.
- c = r & 3 bytes follow: they are the **low** bytes missing from a 24-bit count of 100-ns units, i.e.
  `frac = sum(byte[k] << (8 * (3 - c + k)) for k in 0..c-1)`; add `frac * 100` ns. (Derived; see section 15.)

**ATTR**: for HOST_OS 0-2 the MS-DOS/Windows attributes (0x01 read-only, 0x02 hidden, 0x04 system, 0x10
directory, 0x20 archive); for HOST_OS 3-5 a Unix `st_mode` (0xA000 in `mode & 0xF000` = symbolic link).
LA4 rejects other host values; a reader may treat them as MS-DOS.

**Directories**: dictionary field = 7 (flags & 0x00E0 == 0x00E0); they have no data. (For MS-DOS hosts the 0x10
attribute is also set.)

**Unix symbolic links** (LA4:`read_symlink_stored`): HOST_OS 3-5 and `ATTR & 0xF000 == 0xA000`. The file data is
the link target (stored in all fixtures; LA4 assumes stored). `rar.rar` has `testlink`.

### 2.6 End of archive (0x7B)

Flag `0x0001`: the archive continues in the next volume. The volume end headers in the fixtures also carry flags
0x0002, 0x0004, 0x0008 with extra fields (observed 0x400E / 0x400F); the sources give them no meaning. Skip the
header by HEAD_SIZE (plus ADD_SIZE when `0x8000`). Nothing after an end header belongs to the archive.

An end header is optional: `rar_noeof.rar` ends after its last file block, which must read successfully
(LA4 returns end-of-archive when no 7 bytes are left; RD reports end when no block header can be read).

### 2.7 Reading loop (RAR 4)

```
read signature (section 1)
loop:
    if fewer than 7 bytes remain: end of archive (no end header)
    if headers are encrypted (main flag 0x0080, seen after the main header): read the next header as in 6.1.3
    else read 7 bytes, then HEAD_SIZE - 7 more; check HEAD_CRC (mismatch = corrupt header, or wrong password)
    switch HEAD_TYPE:
      0x73: main header; remember volume/solid/encrypted/naming flags
      0x74: file header: return the entry; its data area follows
      0x7B: end of archive (or of this volume, flag 0x0001: open the next volume, section 4)
      else: skip header and data
```

The header CRC of the main header and of all others covers bytes 2..HEAD_SIZE-1 (LA4 computes it over the whole
header including any fields it does not parse).

---

## 3. RAR 5 headers

Sources: TN (all header sections; the authority for layouts); LA5:`read_var`, `process_base_block`,
`process_head_main`, `process_head_file`, `process_head_file_extra` and the `parse_file_extra_*` functions;
RD:`archive50.go` (`readBlockHeader`, `parseFileHeader`, `parseArcBlock`, `parseEncryptionBlock`,
`parseFileEncryptionRecord`, `parseFilePrecisionTimeRecord`, `parseFileRedirectionRecord`,
`parseFileOwnerRecord`). Checked against all RAR 5 fixtures.

### 3.1 vint

A vint stores an unsigned integer 7 bits per byte, least significant group first; bit 7 of each byte is set when
another byte follows (TN "vint"). Up to 10 bytes (64-bit values). A writer may pad with `0x80` bytes (a zero group
with the continuation bit). LA5 stops after 9 bytes; RD uses Go's `Uvarint` (10). Accept up to 10 and reject
longer.

```
value = 0; shift = 0
repeat: b = next byte; value |= (b & 0x7F) << shift; shift += 7; until (b & 0x80) == 0
```

### 3.2 General block layout

| Field | Type | Meaning |
|---|---|---|
| Header CRC32 | u32 | CRC-32 (5.1) of everything from "Header size" to the end of the extra area |
| Header size | vint | size of the header from "Header type" to the end of the extra area; at most 3 bytes long (header <= 2 MiB) |
| Header type | vint | 1 main, 2 file, 3 service, 4 archive encryption, 5 end of archive |
| Header flags | vint | see below |
| Extra area size | vint | present if flag 0x0001 |
| Data size | vint | present if flag 0x0002 |
| (type-specific fields) | | |
| Extra area | | the last "Extra area size" bytes of the header: a list of records |
| Data area | | "Data size" bytes after the header; not covered by the header CRC |

So the whole header occupies `4 + len(vint Header size) + Header size` bytes, and the CRC covers
`len(vint) + Header size` bytes starting at the size field. LA5 rejects header size 0; a header must be at least
large enough for type and flags.

Common header flags (TN; LA5 `HFL_*`; RD `block5*`):

| Flag | Meaning |
|---|---|
| 0x0001 | extra area present |
| 0x0002 | data area present |
| 0x0004 | skip this block if its type is unknown (when updating) |
| 0x0008 | data area continues from the previous volume (split before) |
| 0x0010 | data area continues in the next volume (split after) |
| 0x0020 | block depends on the preceding file block |
| 0x0040 | preserve a child block if the host block is modified |

Unknown header types: skip header and data area. (LA5 fails on an unknown type without 0x0004; TN only says
0x0004 matters "when updating". A reader may skip any unknown type.)

**Extra area records** (TN "General extra area format"): each record is `Size (vint)`, `Type (vint)`, data;
Size counts from Type to the end of the record. Unknown record types must be skipped.

### 3.3 Archive encryption header (type 4)

Present only in archives with encrypted headers, immediately after the signature (TN). Fields after the common
ones:

| Field | Type | Meaning |
|---|---|---|
| Encryption version | vint | 0 = AES-256; anything else is unsupported |
| Encryption flags | vint | 0x0001: password check value present |
| KDF count | u8 | binary logarithm of the PBKDF2 iteration count |
| Salt | 16 bytes | |
| Check value | 12 bytes | present if flag 0x0001 (section 6.2.2) |

Every header after this one is encrypted as described in 6.2.4. This header itself is plain.

### 3.4 Main archive header (type 1)

| Field | Type | Meaning |
|---|---|---|
| Archive flags | vint | 0x0001 volume; 0x0002 volume number field present (all volumes but the first); 0x0004 solid; 0x0008 recovery record present; 0x0010 locked |
| Volume number | vint | present if 0x0002: 1 for the second volume, 2 for the third, ... (the first has none, i.e. 0) |
| Extra area | | records: 0x01 locator, 0x02 metadata |

Locator record (type 1): `Flags (vint)` (0x0001 quick-open offset present, 0x0002 recovery-record offset present),
then the offsets as vints (distance from the start of that service block to the start of the main header; 0 means
unknown). Metadata record (type 2): `Flags (vint)` (0x0001 name present, 0x0002 creation time present, 0x0004 Unix
time else Windows FILETIME, 0x0008 Unix time in nanoseconds), `Name length (vint)` + UTF-8 name (truncate at the
first zero byte) if 0x0001, time (u64 FILETIME, u32 Unix seconds, or u64 Unix nanoseconds) if 0x0002. A reader
needs neither; LA5 rejects any main-header extra record other than the locator, which is too strict: skip unknown
records.

### 3.5 File header (type 2) and service header (type 3)

Fields after the common ones (TN; order matters):

| Field | Type | Meaning |
|---|---|---|
| File flags | vint | 0x0001 directory; 0x0002 mtime (u32 Unix) present; 0x0004 data CRC32 present; 0x0008 unpacked size unknown |
| Unpacked size | vint | present always; ignore it when 0x0008 is set (decode to the end of the compressed stream) |
| Attributes | vint | host-specific (3.7) |
| mtime | u32 | Unix seconds; present if 0x0002 |
| Data CRC32 | u32 | present if 0x0004; CRC-32 of the unpacked data (split files: 4.3; encrypted with MAC: 6.2.5) |
| Compression information | vint | below |
| Host OS | vint | 0 Windows, 1 Unix |
| Name length | vint | |
| Name | bytes | UTF-8, no terminator, `/` as separator (backslash is a name character on Unix hosts, invalid on Windows) |
| Extra area, data area | | as in 3.2 |

The data area of a file header is the packed file (its size is the header's Data size). File headers always have
flag 0x0002 (LA5 rejects a file header without it, except that directories have data size 0).

**Compression information** (TN; LA5:`process_head_file`; RD:`parseFileHeader`):

| Bits (mask) | Meaning |
|---|---|
| 0-5 (0x003F) | algorithm version: 0 = RAR 5.0 algorithm; 1 = RAR 7.0 algorithm (12.8); others unsupported |
| 6 (0x0040) | solid: continue the window and state of the previous file (7.5) |
| 7-9 (0x0380) | method: 0 stored, 1..5 compressed (all use the same decoder) |
| 10-14 (0x7C00) | dictionary size N: 128 KiB << N. Version 0 uses N <= 15 (4 GiB); version 1 allows up to 23 |
| 15-19 (0xF8000) | version 1 only: fraction F; dictionary += (dictionary / 32) * F |
| 20 (0x100000) | version 1 only: the data is really version-0 data (dictionary field in version-1 form) |

(TN numbers bits from 1, so its "bits 8-10" are bits 7-9 here; its masks agree with this table.) Directories store
0 here. LA5 masks N with 0x0F and refuses dictionaries above 64 MiB; RD allows up to 64 GiB but defaults to a
4 GiB limit. A decoder should refuse dictionaries above a configurable limit before allocating.

Service headers (type 3) use the same layout; their name tells what they hold: `CMT` archive comment (stored, UTF-8,
before the first file), `QO` quick-open data (after the files), `ACL`, `STM`, `RR`. Readers that only extract files
skip them, data area included. The quick-open data (TN "Quick open header") caches copies of headers; TN warns that
a reader must use the same access path for listing and extracting, so the simplest correct choice is never to use
it.

### 3.6 File and service extra records

| Type | Name | Layout (after Size and Type) |
|---|---|---|
| 0x01 | encryption | `Version (vint)` (0 = AES-256), `Flags (vint)` (0x0001 check value present; 0x0002 checksums are MAC'd), `KDF count (u8)`, `Salt (16)`, `IV (16)`, `Check value (12)` if 0x0001. Section 6.2 |
| 0x02 | hash | `Hash type (vint)`: 0 = BLAKE2sp, followed by 32 bytes of hash. Section 5.3 |
| 0x03 | time | `Flags (vint)`: 0x0001 Unix time_t (else Windows FILETIME), 0x0002 mtime, 0x0004 ctime, 0x0008 atime, 0x0010 Unix nanoseconds. Then mtime, ctime, atime as present (u32 if Unix, u64 FILETIME otherwise), then, if 0x0001 and 0x0010 are both set, one u32 nanosecond value for each time that is present, in the same order |
| 0x04 | version | `Flags (vint)` (0), `Version number (vint)`; LA5 appends `;N` to the name |
| 0x05 | redirection | `Type (vint)`: 1 Unix symlink, 2 Windows symlink, 3 Windows junction, 4 hard link, 5 file copy; `Flags (vint)`: 0x0001 target is a directory; `Name length (vint)`, target name (UTF-8) |
| 0x06 | Unix owner | `Flags (vint)`: 0x0001 user name, 0x0002 group name, 0x0004 numeric uid, 0x0008 numeric gid; then each present item: names as `length (vint)` + bytes, ids as vint |
| 0x07 | service data | service-specific bytes |

Notes from the readers: FILETIME is 100-ns units since 1601-01-01; subtract 116444736000000000 for the Unix epoch
(RD:`readWinFiletime`). RD masks a nanosecond value with 0x3FFFFFFF and rejects values >= 10^9. LA5 reads only one
nanosecond field however many times are present, which is wrong by TN; follow TN. RD defines the gid flag as 0x06,
a typo; TN says 0x0008. LA5 caps owner names at 255 bytes but skips the whole stored length; RD stops at 255 bytes
and mis-positions the next field. Use TN: always consume the stored length.

### 3.7 Attributes and host OS

Host 0 (Windows): attributes 0x01 read-only, 0x02 hidden, 0x04 system, 0x10 directory, 0x20 archive (LA5 maps these
to modes 0444/0644/0555/0755). Host 1 (Unix): a Unix `st_mode`. LA5 rejects other hosts; treat them as unknown.
`rar5_fileattr.rar` has read-only, hidden, system and directory attributes; `rar5_win32.rar` has Windows FILETIME
time records.

### 3.8 End of archive header (type 5)

`End of archive flags (vint)`: 0x0001 = this volume is not the last. Nothing after it is read (TN: tools may append
data such as signatures).

### 3.9 Reading loop (RAR 5)

```
read signature (section 1)
key = none
loop:
    if key: read the next header as in 6.2.4 (16-byte IV, then whole AES blocks)
    else:   read u32 CRC, vint size, then size bytes
    check CRC; parse type and flags (and extra/data sizes)
    switch type:
      4: derive the header key from salt and KDF count (6.2.1), verify the check value; key = it
      1: main header; volume number must be 0 for the first volume opened, then 1, 2, ... (4.2)
      2: file header: return the entry; its data area follows
      3: service header: skip its data area
      5: end: if flag 0x0001 and a next volume exists, continue in it (section 4); else end of archive
      else: skip
```

---

## 4. Volumes

Sources: LA4:`archive_read_format_rar_read_data_skip`, `rar_read_ahead`, `read_header` (split-file continuation);
LA5:`advance_multivolume`, `merge_block`, `process_block`, `do_unstore_file`, `process_head_main`;
RD:`volume.go` (`nextNewVolName`, `nextOldVolName`, `openVolumeFile`, `fileVolume.nextBlock`),
`reader.go:packedFileReader.nextBlock`. Split-part checksums checked against the volume fixtures.

### 4.1 Names and order

- RAR 5, and RAR 4 with main flag 0x0010: **new naming**. Volumes are `name.part1.rar`, `name.part2.rar`, ... with
  the number zero-padded to the same width in every volume (`part01`, `part001`, ...). To get the next name,
  increment the last run of decimal digits before the extension, keeping its width (RD:`nextNewVolName`, which
  also handles names like `x.part3of5.rar` by taking the first of the last two numbers).
- RAR 4 without 0x0010: **old naming**. The first volume is `name.rar`, then `name.r00`, `name.r01`, ...,
  `name.r99`, `name.s00`, ... : the extension's last two characters are a decimal counter and the first character
  is incremented on overflow (RD:`nextOldVolName`). (An SFX first volume `name.exe` is followed by `name.r00`.)
- RAR 5 main headers carry the volume number (3.4); check that volume k has number k (k = 0 for the first). RAR 4
  has only the "first volume" flag 0x0100 in the main header.

Every volume begins with the signature and a main header (RAR 5 with encrypted headers: the encryption header,
then the main header; the header key is derived again per volume). Each volume ends with an end-of-archive header
with flag 0x0001 if another volume follows.

### 4.2 Files split across volumes

A file whose data does not fit is cut into parts. Each part is a complete file header (same name; RAR 4 flags
0x0001/0x0002, RAR 5 header flags 0x0008/0x0010) followed by that part's slice of the packed data. The packed data
of the parts, concatenated in volume order, is the file's packed stream: one compressed stream, one encryption
stream (a single AES-CBC chain started with the first part's key and IV; the slices are not multiples of 16 bytes
on their own, e.g. `rar4_vol_enc.part01.rar`/`part02.rar` hold 10108 + 1956 = 12064 bytes of one file). Take the
algorithm, sizes, encryption parameters and solid flag from the **first** part's header; RD takes the MAC key
(6.2.5) and the final checksum from the **last** part's header.

A continuation part must have the same name as the file being read; a part without "split before" where one is
expected, or with a different name, is an error (LA4:`read_header` "Mismatch of file parts", RD
`ErrInvalidFileBlock`). Normally the continuation is the first file header of the next volume; RAR 5 allows
service headers in between, which are skipped (LA5:`advance_multivolume`).

In RAR 5 a compressed block (12.1) may itself straddle volumes: its header can be in one volume and its body in the
next. A decoder that reads the packed stream through a "concatenated parts" byte source needs nothing special; LA5
merges the pieces into one buffer (`merge_block`).

### 4.3 Checksums of split files

Checked on `rar4_vol*`, `rar4_oldvol*`, `rar4_vol_enc*`, `rar5_vol*`, `rar5_vol_enc*`, `rar5_multiarchive_solid*`
(matches TN "Data CRC32" and "File hash record"):

- In every part except the last, the header's CRC32 (RAR 4 FILE_CRC, RAR 5 Data CRC32) and a RAR 5 hash record hold
  the checksum of **that part's packed bytes as stored** (after encryption, if encrypted).
- In the last part they hold the checksum of the whole unpacked file (MAC'd as in 6.2.5 when so flagged).

Checking the per-part values is optional; the final one must be checked.

---

## 5. Checksums

### 5.1 CRC-32

The CRC-32 of ISO 3309 / ITU-T V.42 (the one in zlib and PNG): reflected polynomial 0xEDB88320, initial value
0xFFFFFFFF, final XOR 0xFFFFFFFF, bytes processed least significant bit first. Check value: CRC-32 of the ASCII
string `123456789` is 0xCBF43926. (LA4 `CRC_POLYNOMIAL`; LA4/LA5 call zlib's `crc32`; RD uses Go's IEEE CRC.)

```
crc = 0xFFFFFFFF
for each byte b: crc ^= b; repeat 8 times: crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
result = crc ^ 0xFFFFFFFF
```

Uses: RAR 4 header CRC (low 16 bits, 2.1), RAR 4 file CRC, RAR 5 header CRC, RAR 5 data CRC, and the RAR 3
filter fingerprints (10.3). Stored as u32 little-endian.

### 5.2 Where checksums are checked

- RAR 4: FILE_CRC against the CRC-32 of the unpacked output (exactly UNP_SIZE bytes). Directories store 0.
- RAR 5: if file flag 0x0004, the Data CRC32 against the CRC-32 of the output; if a hash record (type 0x02) is
  present, its BLAKE2sp value against the BLAKE2sp of the output. Both may be present.
- Encrypted RAR 5 files with encryption-record flag 0x0002 store MAC'd values (6.2.5) in both places.
- Split files: 4.3. Header CRCs: always (a mismatch on an encrypted header means a wrong password or damage).

LA5 skips the CRC check when the stored CRC happens to be 0 (`update_crc` tests `stored_crc32 > 0`); do not copy
that: test the flag.

### 5.3 BLAKE2sp

Sources: LA5:`parse_file_extra_hash`, `update_crc`, `verify_checksums` (BLAKE2sp, 32-byte output, over the
unpacked data); TN "File hash record". The construction below is BLAKE2sp as defined in the BLAKE2 paper
(Aumasson, Neves, Wilcox-O'Hearn, Winnerlein, "BLAKE2: simpler, smaller, fast as MD5", 2013) on top of BLAKE2s
(RFC 7693). It was checked in this session against all 256 unkeyed BLAKE2sp vectors of the BLAKE2 reference test
set (`testvectors/blake2-kat.json`).

BLAKE2s (RFC 7693) is initialised with `h[i] = IV[i] ^ P[i]` for i = 0..7, where P is the 32-byte parameter block
read as eight u32 little-endian words. RFC 7693 only describes the sequential case (P has digest length and key
length); BLAKE2sp needs the full parameter block:

| Byte offset | Field | Leaf i (i = 0..7) | Root |
|---|---|---|---|
| 0 | digest length | 32 | 32 |
| 1 | key length | 0 | 0 |
| 2 | fanout | 8 | 8 |
| 3 | depth | 2 | 2 |
| 4-7 | leaf length (u32) | 0 | 0 |
| 8-13 | node offset (48-bit LE) | i | 0 |
| 14 | node depth | 0 | 1 |
| 15 | inner length | 32 | 32 |
| 16-23 | salt | 0 | 0 |
| 24-31 | personalisation | 0 | 0 |

The compression function is RFC 7693's F, plus the **last-node flag**: when the final block of a node marked
"last node" is compressed, `v[15] ^= 0xFFFFFFFF` in addition to RFC 7693's `v[14] ^= 0xFFFFFFFF`. Leaf 7 and the
root are last nodes; leaves 0..6 are not.

```
split the message into 64-byte chunks c0, c1, ... (the last may be shorter; an empty message has none)
leaf i absorbs chunks i, i+8, i+16, ... in order (BLAKE2s update), then is finalised -> 32-byte digest Di
    (a leaf that received nothing still finalises: it hashes the empty string with its own parameters)
root absorbs D0 || D1 || ... || D7 (256 bytes) and is finalised -> the 32-byte BLAKE2sp value
```

Test vectors (unkeyed, from the reference set): empty input ->
`dd0e891776933f43c7d032b08a917e25741f8aa9a12c12e1cac8801500f2ca4f`; input `00 01 02` ->
`ed14413b40da689f1f7fed2b08dff45b8092db5ec2c3610e02724d202f423c46`.

Fixture: `rar5_blake2.rar` (`cebula.txt`, 814 bytes, CRC-32 7e5ec49e; the archive stores only the BLAKE2sp hash).

---

## 6. Encryption

Sources: RD:`archive15.go` (`calcAes30Params`, `getKeys`, `readBlockHeader`), RD:`archive50.go` (`calcKeys50`,
`getKeys`, `parseEncryptionBlock`, `parseFileEncryptionRecord`, `readBlockHeader`), RD:`decrypt_reader.go`
(`newAesDecryptReader`: AES in CBC mode), RD:`reader.go` (`checksumReader.eofError`: MAC'd checksums,
`newArchiveFileFrom`: decrypt, then decompress, then truncate to the unpacked size); TN (encryption header and
record). LA4 and LA5 only detect encryption. All derivations below were checked on the fixtures: the RAR 3 key
decrypts `rar4_enc_headers.rar` headers with valid CRCs, the RAR 5 check values match in every `rar5_enc_*`
fixture, and the MAC'd CRC of `rar5_enc_data.rar` matches.

Standards: AES (FIPS 197), CBC mode (NIST SP 800-38A, as FIPS 197 users know it), SHA-1 and SHA-256 (FIPS 180-4),
HMAC (RFC 2104), PBKDF2 (RFC 8018).

### 6.1 RAR 4 (RAR 3.x encryption)

Used when FILE flag 0x0004 is set **and** a salt is present (flag 0x0400), and for header encryption (main flag
0x0080). Encrypted entries without a salt use older ciphers (RAR 1.5/2.0), which are out of scope.

#### 6.1.1 Key and IV derivation

Input: password as **UTF-16LE** (no terminator; RD truncates passwords to 128 characters), 8-byte salt.

```
P = utf16le(password) || salt
sha = SHA-1 context
iv = 16 bytes
for i in 0 .. 0x3FFFF:                         # 262144 rounds
    sha.update(P)
    sha.update(bytes([i & 0xFF, (i >> 8) & 0xFF, (i >> 16) & 0xFF]))   # i as 3 bytes, little-endian
    if i % 0x4000 == 0:
        iv[i / 0x4000] = byte 19 of SHA-1 digest of everything absorbed so far   # snapshot; sha continues
digest = SHA-1 digest of everything absorbed (20 bytes)
key = digest[0..15], with each 4-byte group reversed: key = d3 d2 d1 d0 d7 d6 d5 d4 d11 d10 d9 d8 d15 d14 d13 d12
```

AES-128 with this key, CBC mode, this IV. The IV is never stored. The derivation is slow by design; cache results
by salt (RD keeps the last 4).

Test vector (from `rar4_enc_headers.rar`, password `pass`, salt of the first encrypted header):
salt `397d6db7a1ca0f3a` -> key `21e0fb770f14dc9881aca33844d539fe`, IV `257613f67a8394e6da2c601041bcdcfd`.

#### 6.1.2 Encrypted file data

The packed data area (PACK_SIZE bytes, a multiple of 16; for split files the sum over all parts is) is AES-128-CBC
ciphertext of the packed stream padded to 16 bytes. Decrypt it as one CBC chain with the file's key and IV, feed the
plaintext to the decompressor (or, if stored, use it directly) and stop after UNP_SIZE output bytes; the padding is
ignored. FILE_CRC is the plain CRC-32 of the unpacked data (no MAC in RAR 4).

There is no password check value in RAR 4. A wrong password shows up as a corrupt stream or a CRC mismatch.

Fixtures: `rar4_enc_data.rar`, `rar4_enc_solid.rar`, `rar4_enc_stored.rar`, `rar4_vol_enc.part*.rar` (password
`pass`), `rar4_enc_hangul.rar` (password U+BE44 U+BC00 U+0020 U+BC88 U+D638, UTF-16LE
`44 be 00 bc 20 00 88 bc 38 d6`), `rar4_encrypted.rar` (libarchive's; two of four files encrypted; password not
known: use it for "entry is encrypted" detection).

#### 6.1.3 Encrypted headers

When the main header has flag 0x0080, every block header after the main header (in every volume, including the
end-of-archive header) is stored as:

```
8 bytes  salt
n bytes  AES-128-CBC ciphertext, n = HEAD_SIZE rounded up to a multiple of 16
```

Derive key and IV from the salt (6.1.1), decrypt the first 16 bytes to read HEAD_SIZE (offset 5), decrypt the rest
of the n bytes as the same CBC chain, check HEAD_CRC over the first HEAD_SIZE plaintext bytes, discard the padding.
The data area that follows (ADD_SIZE / PACK_SIZE bytes) is **not** header-encrypted; file data is encrypted
separately when the file flag says so (with the salt in its file header). The signature and the main header are
plain. A header CRC mismatch on the first encrypted header means a wrong password.

Decrypted start of the first header of `rar4_enc_headers.rar` with the test-vector key above:
`20 35 74 24 94 36 00 ...` (CRC 0x3520, type 0x74, flags 0x9424, size 54).

Fixtures: `rar4_enc_headers.rar`, `rar4_vol_enc.part*.rar` (password `pass`); `rar_encryption_header.rar`
(libarchive's; password not known; detection only).

### 6.2 RAR 5 encryption

#### 6.2.1 Key derivation

Input: password as **UTF-8** bytes (RD truncates to 128 characters), the 16-byte salt and KDF count n from the
archive encryption header (3.3) or the file encryption record (3.6). RD refuses n > 24; refuse large values (the
cost is 2^n HMAC-SHA-256 calls).

Three 32-byte values are taken from one PBKDF2-HMAC-SHA-256 chain (RFC 8018, first output block only; the
password is the HMAC key):

```
U1 = HMAC-SHA256(password, salt || 00 00 00 01)
Uj = HMAC-SHA256(password, U(j-1))
Key      = U1 ^ U2 ^ ... ^ U(2^n)              = PBKDF2(password, salt, 2^n,      32)
HashKey  = U1 ^ ... ^ U(2^n + 16)               = PBKDF2(password, salt, 2^n + 16, 32)
PswCheck = U1 ^ ... ^ U(2^n + 32)               = PBKDF2(password, salt, 2^n + 32, 32)
```

(RD computes them in one pass: after 2^n iterations it saves Key, continues 16 more for HashKey, 16 more for the
password check.) Key is the AES-256 key. HashKey is the MAC key of 6.2.5.

#### 6.2.2 Password check value

```
check[0..7] = 0
for j in 0..31: check[j & 7] ^= PswCheck[j]
stored 12 bytes = check[0..7] || first 4 bytes of SHA-256(check[0..7])
```

If the stored value's last 4 bytes do not equal the first 4 bytes of SHA-256 of its first 8, the check data is
damaged (TN: "distinguish invalid password and damaged data"); otherwise, if the first 8 bytes differ from the
computed `check`, the password is wrong. RD compares all 12 bytes, which is equivalent for intact data.

Test vector (from `rar5_enc_headers.rar`, password `pass`): KDF count 15, salt
`2f223c211e51e3f4168a8ffae88c4e4d` -> Key `e8688d4033dc9f21b72c34a157dad1578e6a3123967d2f61a2601fac9356df83`,
HashKey `32b37b4ede957c6aafd578ee3b8bedb94cf71783648b9949dda17aa899e839ad`, stored check value
`82c76b3436bd19e1518dddfb`.

#### 6.2.3 Encrypted file data

A file with an encryption record (3.6): the data area is AES-256-CBC ciphertext with Key from the record's salt and
KDF count and the record's IV. As in 6.1.2: decrypt as one chain (over all parts of a split file), decompress or
use directly, stop at the unpacked size, ignore padding. Data sizes are multiples of 16 (`rar5_enc_stored.rar`:
8200-byte file, 8208 bytes of data). Derive keys once per file (RD caches by salt and count; the first part of a
split file is enough for Key).

#### 6.2.4 Encrypted headers

After an archive encryption header (3.3), each header is stored as:

```
16 bytes  IV for this header
n bytes   AES-256-CBC ciphertext (Key from the encryption header), n = header length rounded up to 16
```

Decrypt the first 16 ciphertext bytes to read the CRC and the size vint, compute the header length
(`4 + len(vint) + size`), decrypt the remaining blocks, check the CRC over the plaintext header, discard the padding.
Data areas are not header-encrypted (file data is encrypted per file, 6.2.3). Each volume repeats the encryption
header and derives the key again (RD resets the header key per volume).

Fixtures: `rar5_enc_headers.rar`, `rar5_vol_enc.part*.rar` (password `pass`), `rar5_encrypted_filenames.rar`
(libarchive's; password `password`, verified by its check value).

#### 6.2.5 MAC'd checksums

When the file encryption record has flag 0x0002, the stored Data CRC32 and BLAKE2sp hash are replaced by values
keyed with HashKey (TN: "tweaked checksums"). Compute the plain checksum of the unpacked data, then:

```
raw = the CRC-32 as 4 bytes little-endian, or the 32-byte BLAKE2sp value
mac = HMAC-SHA256(HashKey, raw)                       # 32 bytes
CRC32 case:   m = mac[0..3]; for j in 4..31: m[j & 3] ^= mac[j]; compare m (as u32 LE) with the stored CRC32
BLAKE2 case:  compare mac (32 bytes) with the stored hash
```

(RD:`checksumReader.eofError`; it uses the HashKey of the last part of a split file.) Checked: `rar5_enc_data.rar`
`noise.bin`, plain CRC 1fa12d9d, stores ebc748f4, and HMAC with the HashKey from its record folds to ebc748f4.
In the fixtures, `-p` archives set flag 0x0002 and `-hp` archives (`rar5_enc_headers`, `rar5_vol_enc`) store plain
CRCs.

Fixtures: `rar5_enc_data.rar`, `rar5_enc_solid.rar`, `rar5_enc_stored.rar` (password `pass`),
`rar5_enc_hangul.rar` (password U+BE44 U+BC00 U+0020 U+BC88 U+D638, UTF-8 `eb b9 84 eb b0 80 20 eb b2 88 ed 98 b8`;
check value verified), `rar5_encrypted.rar` (libarchive's: `b.txt` uses password `password`, check value verified;
`d.txt` uses another, unknown one; `a.txt` and `c.txt` are not encrypted).

---
