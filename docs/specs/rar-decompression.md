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

The block occupies HEAD_SIZE bytes, then its data area (ADD_SIZE bytes, or PACK_SIZE for file blocks). The data
area is not covered by HEAD_CRC (but see section 15 #18 for LA4's handling of skippable blocks). To skip a
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

**EXT_TIME** (LA4:`read_exttime`, RD:`readExtTimes`): a u16 FLAGS, then for each of four times in this order --
mtime (FLAGS bits 12-15), ctime (bits 8-11), atime (bits 4-7), archive time (bits 0-3) -- a 4-bit field r:

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

The data area of a file header is the packed file (its size is the header's Data size). File headers carry flag
0x0002 even when empty (directories have data size 0); LA5 rejects a file header without it.

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

Standards: AES (FIPS 197), SHA-1 and SHA-256 (FIPS 180-4), HMAC (RFC 2104), PBKDF2 (RFC 8018). CBC decryption
(NIST SP 800-38A), as used throughout: split the ciphertext into 16-byte blocks C1, C2, ...; with C0 = IV,
plaintext block `Pi = AES-decrypt(key, Ci) XOR C(i-1)`. A CBC "chain" continues across reads: the last ciphertext
block of one read is the C(i-1) of the next. No padding scheme is used; trailing bytes are cut by the known sizes.

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

## 7. Common decompression machinery

### 7.1 Bit input

Sources: LA4:`rar_br_fillup`, `rar_br_bits`; LA5:`read_bits_16`, `read_bits_32`, `skip_bits`;
RD:`bit_reader.go` (`rarBitReader`, `rar5BitReader`).

All three algorithms read their packed stream as a bit stream, most significant bit of each byte first (see
Conventions). The decoders peek up to 16 bits (Huffman lookup) and then consume fewer. Near the end of the data a
peek may run past the last byte; treat missing bits as zero for peeking, but consuming bits beyond the end of the
packed data is a truncated-data error. Byte reads inside the bit stream (filter records, PPMd bytes) read 8 bits;
they are byte-aligned only where stated.

### 7.2 Canonical Huffman codes

Sources: LA4:`create_code`, `add_value`, `make_table`, `read_next_symbol`; LA5:`create_decode_tables`,
`decode_number`; RD:`huffman.go` (`huffmanDecoder.init`, `readSym`).

Every Huffman table is transmitted as a list of code lengths, one per symbol, 0 = symbol unused, maximum 15.
Codes are canonical: sort the used symbols by (length, symbol number); the first gets the all-zero code of its
length; each next code is the previous code + 1, shifted left by the length difference when the length grows.
Codes are read bit by bit from the stream, first bit = most significant bit of the code.

```
count[l] = number of symbols with length l (l = 1..15)
code = 0; for l in 1..15: first_code[l] = code; code = (code + count[l]) << 1
symbols of length l get codes first_code[l], first_code[l]+1, ... in increasing symbol order
```

Incomplete code sets (unused code space, including a single used symbol) occur and must be accepted; reading an
unused code is a data error (RD returns `ErrHuffDecodeFailed`; LA4 "Invalid prefix code"; LA5 silently yields
symbol 0 -- do not copy that). An over-subscribed set (more codes than fit) is a data error.

### 7.3 The precode (code-length tables), RAR 2.9 and RAR 5

Sources: LA4:`parse_codes`; LA5:`parse_tables`; RD:`huffman.go:readCodeLengthTable`.

The code lengths of the main tables are themselves sent with a 20-symbol "bit-length" code:

```
# 1. lengths of the 20 precode symbols, 4 bits each, with a zero-run escape
i = 0
while i < 20:
    v = read 4 bits
    if v == 15:
        z = read 4 bits
        if z == 0: bl[i] = 15; i += 1                    # a real length 15
        else:      bl[i .. i+z+1] = 0; i += z + 2        # z + 2 zeros (clipped at 20)
    else: bl[i] = v; i += 1
build the canonical code from bl

# 2. the table lengths, N entries (N given per algorithm)
i = 0
while i < N:
    s = next precode symbol
    if s < 16:  len[i] = s (RAR 5), or (old[i] + s) & 15 (RAR 2.9, see 9.2); i += 1
    if s == 16: n = 3 + read 3 bits;  repeat previous length n times     # error if i == 0
    if s == 17: n = 11 + read 7 bits; repeat previous length n times     # error if i == 0
    if s == 18: n = 3 + read 3 bits;  write n zeros
    if s == 19: n = 11 + read 7 bits; write n zeros
    (repeats are clipped at N)
```

RAR 2.0 uses a different precode, section 8.2.

### 7.4 The window

Sources: LA4:`lzss_emit_literal`, `lzss_emit_match`, `expand`; LA5:`copy_string`, `do_uncompress_block`;
RD:`decode_reader.go` (`decodeReader.writeByte`, `copyBytes`, `init`).

Output goes through a circular window W of size `wsize`, a power of two at least the dictionary size of the file
(RD uses at least 256 KiB; any larger power of two is equally correct). For a new non-solid file the window is
filled with zeros and the write position is 0.

- Literal b: `W[pos & (wsize-1)] = b; pos += 1`.
- Match (length L, distance d): for k in 0..L-1: `W[(pos+k) & m] = W[(pos+k-d) & m]` with `m = wsize - 1`, copied
  **one byte at a time in increasing order**, so that d < L repeats a pattern (d = 1 repeats the last byte).

A distance larger than the number of bytes written so far in the current solid stream reads zeros (or data of
earlier files in a solid archive); it does not occur in valid archives.

Output: bytes leave the window in order, after filters (sections 10 and 12.5) have been applied to the ranges
they cover. The reader must not overwrite window bytes that have not been output yet (LA4 limits each decoding
burst to `wsize - 260` bytes, LA5 to `wsize / 2`).

### 7.5 End of file, unpacked size, solid streams

Sources: LA4:`read_data_compressed`; LA5:`do_uncompress_file`, `reset_file_context`, `process_head_file`;
RD:`reader.go:newArchiveFileFrom` (`limitedReader`), RD:`decode_reader.go:init`, RD:`decode29.go:init`,
RD:`decode50.go:init`, RD:`decode20.go:init`.

- Produce exactly the unpacked size given in the header. A decoder may produce more (a final match can run past
  the end; RAR 2.9 and RAR 5 streams are padded); discard the excess. Packed data that ends before the unpacked
  size is reached is an error ("decoded file too short", RD `ErrShortFile` / `ErrDecoderOutOfData`). With an
  unknown unpacked size, stop at the end-of-file signal (RAR 2.9: 9.4, 11.3; RAR 5: last block, 12.1).
- **In a solid archive, decode each file's stream to its end**, not just to the unpacked size: RAR 2.9 up to and
  including the end-of-file code (LZ symbol 256 with its two bits, 9.4, or PPMd escape code 2, 11.3), RAR 5 through
  the block flagged "last" (12.1). Whatever comes after the last output byte still matters to the next file: the
  end-of-file code's second bit says whether the next file starts with a block header, and a later block may carry
  the tables or filters the next file uses. Bytes decoded beyond the unpacked size go into the window but are not
  output. (RD:`decodeReader.nextFile` drains the decoder whenever the archive is solid; LA5:`rar5_read_data_skip`
  processes blocks while packed bytes remain.) RAR 2.0 has no end-of-file code: RD stops at the unpacked size and
  the next solid file continues the same block (8.3).
- **Solid**: in a solid archive, a file marked solid (RAR 4 file flag 0x0010; RAR 5 compression-information bit
  0x40) continues the previous file's decoder state: the window and its write position, the Huffman tables and
  code lengths, the repeat-distance history and last length, the PPMd model and escape byte, and the filter program
  list (RAR 2.9). Only the packed input changes: the bit reader restarts at the first byte of the new file's packed
  data (no bits carry over). A non-solid file resets all of it. Consequently, extracting file k of a solid archive
  requires decoding files 0..k-1 (their output can be discarded; RD and LA5 do exactly that when skipping).
- RD refuses to change algorithm inside one solid stream (`ErrMultipleDecoders`). RAR 5 solid files must keep the
  window size of the first solid file (LA5 checks this).

---

## 8. RAR 2.0 algorithm (UNP_VER 20 and 26)

Sources: RD only: `decode20.go` (`decoder20.init`, `readBlockHeader`, `fill`, `readCodeLengthTable20`),
`decode20_lz.go`, `decode20_audio.go`; tables shared with `decode29_lz.go`. LA4 has no RAR 2.0 decoder.
**No fixture contains RAR 2.0 compressed data** (the UNP_VER 20 entries in the fixtures are all stored), so this
section is untested here; see section 15.

### 8.1 Block header

A block starts with two bits (no byte alignment):

```
audio = read 1 bit
keep  = read 1 bit          # 0: clear the saved code lengths (all 1028 entries) to zero first
if audio:
    channels = read 2 bits + 1                 # 1..4
    read channels * 257 code lengths (8.2) -> one 257-symbol table per channel
else:
    read 298 + 48 + 28 = 374 code lengths (8.2) -> main (298), distance (48), length (28) tables
```

The saved code-length array (1028 = 4 * 257 entries) persists between blocks and, in solid mode, between files;
new lengths are **added** to the saved ones (8.2). If a new audio block has fewer channels than the current
channel index, the current channel becomes 0.

### 8.2 Code lengths (RAR 2.0 precode)

```
bl[0..18] = 19 values of 4 bits each (no escape)           # precode, 19 symbols
i = 0
while i < N:
    s = next precode symbol
    s < 16:  len[i] = (len[i] + s) & 15; i += 1
    s == 16: n = 3 + read 2 bits; repeat len[i-1] n times   # error if i == 0
    s == 17: n = 3 + read 3 bits; n zeros
    s == 18: n = 11 + read 7 bits; n zeros
    (clip at N)
```

### 8.3 LZ mode

State: last length `L`, four distances `D[0..3]` (D[0] newest), all 0 at the start of a non-solid file (RD does
not reset them; see section 15).

Tables (shared with RAR 2.9, 9.3): `LBASE`, `LBITS` (28 entries), `DBASE`, `DBITS` (first 48 entries used),
`SDBASE`, `SDBITS` (8 entries).

Main symbol s:

| s | Action |
|---|---|
| 0..255 | literal byte s |
| 256 | repeat: `D = [D0, D0, D1, D2]` (push a copy of D0); copy L bytes from distance D[0] |
| 257..260 | j = s - 257; d = D[j]; `D = [d, D0, D1, D2]` (push, not move-to-front); length symbol l from the length table, `L = LBASE[l] + 2 + read LBITS[l] bits`; then `L += 1` if d >= 0x101, another +1 if d >= 0x2000, another +1 if d >= 0x40000; copy L bytes from d |
| 261..268 | j = s - 261; `d = SDBASE[j] + 1 + read SDBITS[j] bits`; push d; L = 2; copy |
| 269 | end of block: read a new block header (8.1) |
| 270..297 | j = s - 270; `L = LBASE[j] + 3 + read LBITS[j] bits`; distance symbol k from the distance table, `d = DBASE[k] + 1 + read DBITS[k] bits`; `L += 1` if d >= 0x2000, another +1 if d >= 0x40000; push d; copy |

"Push d" is `D = [d, D0, D1, D2]`. There is no end-of-file symbol: decoding of a file stops when its unpacked
size is reached; in solid mode the next file continues in the same block.

### 8.4 Audio mode

Each channel c has state `K[0..4]` (weights), `Dl[0..3]`, `lastDelta`, `dif[0..10]`, `count`, `lastChar`, all 0 at
the start of a non-solid file; the decoder has one shared `chanDelta` (0) and the current channel index (0). Symbol
256 of a channel's table ends the block; symbols 0..255 are deltas. For each decoded delta `s` (0..255) of channel c:

```
v = state[c]
v.count += 1
v.Dl[3] = v.Dl[2]; v.Dl[2] = v.Dl[1]; v.Dl[1] = v.lastDelta - v.Dl[0]; v.Dl[0] = v.lastDelta
p = 8*v.lastChar + v.K0*v.Dl0 + v.K1*v.Dl1 + v.K2*v.Dl2 + v.K3*v.Dl3 + v.K4*chanDelta     # signed ints
p = (p >> 3) & 0xFF                                                                    # arithmetic shift
ch = (p - s) & 0xFF                      # output byte
e = signed8(s) << 3                      # s taken as a signed byte
v.dif[0] += |e|
v.dif[1] += |e - v.Dl0|;  v.dif[2]  += |e + v.Dl0|
v.dif[3] += |e - v.Dl1|;  v.dif[4]  += |e + v.Dl1|
v.dif[5] += |e - v.Dl2|;  v.dif[6]  += |e + v.Dl2|
v.dif[7] += |e - v.Dl3|;  v.dif[8]  += |e + v.Dl3|
v.dif[9] += |e - chanDelta|; v.dif[10] += |e + chanDelta|
chanDelta = signed8(ch - v.lastChar); v.lastDelta = chanDelta; v.lastChar = ch
if v.count & 0x1F == 0:
    j = index of the smallest v.dif[0..10] (first one on ties); set all v.dif to 0
    if j > 0: t = (j - 1) / 2
              if (j - 1) is even: if v.K[t] >= -16: v.K[t] -= 1
              else:               if v.K[t] <  16:  v.K[t] += 1
emit ch; c = (c + 1) mod channels
```

`lastChar` holds the output byte (0..255). (RD keeps it as an unmasked int; that gives the same bytes because
only `p & 0xFF` and `signed8(...)` are used.) The output bytes go through the window like literals, so LZ blocks
that follow can refer to them.

---

## 9. RAR 2.9 / 3.x algorithm (UNP_VER 29): blocks and LZ

Sources: LA4:`parse_codes`, `expand`, `read_data_compressed`, `read_filter`; RD:`decode29.go`
(`readBlockHeader`, `fill`), RD:`decode29_lz.go` (`init`, `fill`, `readEndOfBlock`, `decodeOffset`,
`decodeLength`, `decodeShortOffset`, `readFilterData`, the tables).

### 9.1 Blocks

The packed stream of a file is a sequence of blocks. Each block header starts at a **byte boundary** (skip the
remaining bits of the current byte; LA4 `rar_br_consume_unalined_bits`, RD `alignByte`):

```
align to byte
ppm = read 1 bit
if ppm: PPMd block, section 11
else:   LZ block: keep = read 1 bit; read the tables (9.2); decode LZ symbols (9.4)
```

A file's stream starts with a block header, except a solid file that follows a file whose last block ended with
"end of file, keep tables" (9.4): that file starts directly with LZ symbols using the current tables.

### 9.2 LZ tables

`keep` = 0: set the saved code-length array (404 entries) to zeros first. Then read 404 code lengths with the
precode of 7.3, where symbols 0..15 are **added** to the saved length: `len[i] = (old[i] + s) & 15`. (LA4 always
adds after optionally zeroing; RD assigns when keep = 0 and adds when keep = 1; same result.) The array is split:

| Table | Entries | Use |
|---|---|---|
| main (MC) | 299 | literals and commands |
| distance (DC) | 60 | distance slots |
| low distance (LDC) | 17 | low 4 bits of long distances |
| length (RC) | 28 | lengths for repeated distances |

The saved array persists across blocks and, in solid mode, across files. Each LZ table read also resets the
low-distance repeat state (9.4) in RD but not in LA4; see section 15.

### 9.3 Tables of bases and extra bits

These are the same in LA4 (`expand`) and RD (`decode29_lz.go`):

```
LBASE[28] = 0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,40,48,56,64,80,96,112,128,160,192,224
LBITS[28] = 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5

DBASE[60] = 0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072,
            4096,6144,8192,12288,16384,24576,32768,49152,65536,98304,131072,196608,
            262144,327680,393216,458752,524288,589824,655360,720896,786432,851968,917504,983040,
            1048576,1310720,1572864,1835008,2097152,2359296,2621440,2883584,3145728,3407872,3670016,3932160
DBITS[60] = 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,
            15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,16,18,18,18,18,18,18,18,18,18,18,18,18

SDBASE[8] = 0,4,8,16,32,64,128,192
SDBITS[8] = 2,2,3,4,5,6,6,6
```

### 9.4 LZ symbols

State (reset to 0 for a non-solid file): `D[0..3]` distances (D[0] newest), last length `L`, `lowDist`,
`lowRepeat`.

| Main symbol s | Action |
|---|---|
| 0..255 | literal byte |
| 256 | end of block or file, below |
| 257 | filter record, section 10.1 |
| 258 | repeat the last match: if L != 0, copy L bytes from distance D[0] (D unchanged) |
| 259..262 | j = s - 259; d = D[j]; **move to front**: `D[1..j] = D[0..j-1]`, `D[0] = d`; length symbol l from RC, `L = LBASE[l] + 2 + read LBITS[l] bits`; copy L bytes from d (no length bonus) |
| 263..270 | j = s - 263; `d = SDBASE[j] + 1 + read SDBITS[j] bits`; push d (`D = [d, D0, D1, D2]`); L = 2; copy |
| 271..298 | j = s - 271; `L = LBASE[j] + 3 + read LBITS[j] bits`; distance d (below); `L += 1` if d >= 0x2000, another +1 if d >= 0x40000; push d; copy |

Distance for symbols 271..298:

```
k = next DC symbol (0..59)
d = DBASE[k] + 1
b = DBITS[k]
if b >= 4:                                  # k >= 10
    if b > 4: d += (read (b - 4) bits) << 4
    if lowRepeat > 0: lowRepeat -= 1; d += lowDist
    else:
        x = next LDC symbol (0..16)
        if x == 16: lowRepeat = 15; d += lowDist
        else:       d += x; lowDist = x
elif b > 0: d += read b bits
```

Symbol 256 (LA4:`expand`, RD:`readEndOfBlock`):

```
if read 1 bit == 1: end of block: a new block header follows (9.1) in the same file
else:
    end of file.  t = read 1 bit
    t == 1: the next solid file starts with a block header (new tables)
    t == 0: the next solid file starts directly with symbols, reusing the current tables
```

End of file ends this file's stream (in a solid archive, read it even after the last output byte: 7.5). Reaching it before the unpacked size is a short file (an error) unless the
size is unknown; output beyond the unpacked size is discarded (7.5). LA4 treats end of file as "stop" and does not
support solid continuation.

Fixtures: `rar_compress_normal.rar` (METHOD 0x33), `rar_multi_lzss_blocks.rar` (many LZ blocks, 20 MB output),
`rar4_*` compressed fixtures (all LZ, UNP_VER 29), `rar_unicode.rar` (one compressed entry), `rar4_enc_solid.rar`
(solid; encrypted).

---

## 10. RAR 3 filters

Sources: LA4:`read_filter`, `parse_filter`, `compile_program`, `membr_next_rarvm_number`, `run_filters`,
`execute_filter`, `execute_filter_e8`, `execute_filter_delta`, `execute_filter_rgb`, `execute_filter_audio`;
RD:`decode29_lz.go:readFilterData`, `decode29_ppm.go:readFilterData`, `decode29.go:parseVMFilter`, `readVMCode`,
`decode_reader.go:queueFilter`, `processFilters`, `filters.go` (all standard filters, `getV3Filter`,
`vmFilter.execute`), `vm.go`, `bit_reader.go:readUint32`.

RAR 3 transforms ranges of the output with programs for a small virtual machine. The programs that RAR actually
emits are six fixed ones, recognised by checksum and replaced by native code (both sources do this; LA4 supports
five, RD all six plus a full VM). A decoder may implement only the six and report others as unsupported; Appendix A
describes the VM for completeness.

### 10.1 The filter record

In an LZ block, main symbol 257 introduces a filter record; in a PPMd block, escape code 3 does (11.3). The record
is a byte string read through the current decoder (LZ: 8 raw bits per byte from the bit stream, not aligned; PPMd:
one decoded PPMd symbol per byte):

```
flags = next byte
n = (flags & 7) + 1
if n == 7: n = next byte + 7
if n == 8: n = next byte << 8; n |= next byte          # 16-bit big-endian length
data = next n bytes
```

`data` is parsed with its own MSB-first bit reader (reads past its end yield 0 bits and make the record invalid,
LA4 `membr_*`). `vmnum` is RAR 3's variable-length number (LA4:`membr_next_rarvm_number`, RD:`readUint32`):

```
vmnum: t = read 2 bits
  t == 0: return read 4 bits
  t == 1: v = read 8 bits
          if v >= 16: return v
          return 0xFFFFFF00 | (v << 4) | read 4 bits
  t == 2: return read 16 bits
  t == 3: return read 32 bits
```

Parsing (the decoder keeps a list of programs and, per program, its last block length):

```
if flags & 0x80:
    num = vmnum
    if num == 0: forget all programs and pending filters; num = 0
    else: num -= 1
    num must be <= number of known programs                  # == means "a new program"
    last_num = num
else:
    num = last_num
start = vmnum + (number of bytes decoded so far in this file)     # LA4 lzss_position, RD relative to the write index
if flags & 0x40: start += 258
if flags & 0x20: length = vmnum
else:            length = the last length used with program num (0 if new)
R[0..7] = 0; R[3] = 0x3C000; R[4] = length; R[5] = number of earlier filters that used program num; R[7] = 0x40000
if flags & 0x10:
    mask = read 7 bits
    for i in 0..6: if mask & (1 << i): R[i] = vmnum      # bit 0 = R[0]
if num is a new program:
    clen = vmnum; require 1 <= clen <= 0x10000
    code = clen bytes (8 bits each)
    require code[0] == XOR of code[1..clen-1]
    identify the program (10.3); append it to the list
remember length as program num's last length
if flags & 0x08:
    glen = vmnum; require glen <= 0x2000 - 0x40
    global = glen bytes                                    # user global data, used only by the VM (Appendix A)
add filter (program, start, length, R, global) to the pending list
```

Validity checks from the sources: `length` is at most 0x3C000 (LA4: E8/E8E9 need length <= 0x3C000 and > 4;
Delta, RGB, Audio need length <= 0x3C000 / 2); a new filter must not start before the previous pending filter
(RD); when a filter has run, the next pending one must not start before its end (LA4). Treat violations as corrupt
data. RD caps the program list at 1024 and the pending list at 8192 (LA5 also uses 8192 for RAR 5).

### 10.2 Running filters

Filters are applied in the order they were defined. When the decoder has produced all bytes up to
`start + length` of the first pending filter (and has output everything before `start` unchanged):

```
buf = the `length` window bytes at [start, start + length)
out = run the program on buf (10.4), with offset = start (bytes of this file before the block)
while the next pending filter has the same start and its length == len(out):   # chained filters
    out = run that program on out
output out in place of the window bytes [start, start + length)
```

The window itself keeps the **unfiltered** bytes: later matches copy decoded (pre-filter) data. (LA4 runs filters
in a separate VM memory; RD in a separate buffer.)

### 10.3 Standard programs

A program is identified by the CRC-32 (5.1) of its whole byte code (including the leading XOR byte) together with
its length. LA4 combines both in one 40-bit "fingerprint" `crc | length << 32`.

| Filter | CRC-32 | Length | Parameters |
|---|---|---|---|
| E8 | 0xAD576887 | 53 | none |
| E8E9 | 0x3CD7E57E | 57 | none |
| Itanium | 0x3769893F | 120 | none (RD only) |
| Delta | 0x0E06077D | 29 | R[0] = channels |
| RGB | 0x1C2C5DC8 | 149 | R[0] = width (bytes per row), R[1] = position of the red byte (0..2) |
| Audio | 0xBC85E701 | 216 | R[0] = channels |

Other programs: run the VM (Appendix A) or report unsupported (LA4 does the latter).

### 10.4 Standard filter algorithms

In all of them `n = len(buf)` and `off` is the file offset of buf[0].

**E8 and E8E9** (LA4:`execute_filter_e8`, RD:`filterE8`; x86 CALL/JMP relative-to-absolute conversion undone).
`FS = 0x1000000`.

```
i = 0
while i <= n - 5:
    if buf[i] == 0xE8 or (E8E9 and buf[i] == 0xE9):
        pos  = (off + i + 1) mod 2^32                 # file position of the 4 address bytes
        addr = u32 LE at buf[i+1]
        if addr has bit 31 set (negative):
            if (addr + pos) mod 2^32 has bit 31 clear:   store (addr + FS) mod 2^32
        elif addr < FS:                                   store (addr - pos) mod 2^32
        i += 5
    else: i += 1
```

**Itanium** (RD:`itaniumFilterV3`, single source):

```
MASK[16] = 4,4,6,6,0,0,7,7,4,4,0,0,4,4,0,0
fo = off >> 4
p = 0
while n - p > 21:                       # 16-byte bundles; the last bundles of the block are left alone
    t = (buf[p] & 0x1F) - 0x10
    if t >= 0 and MASK[t] != 0:
        for slot in 0..2: if MASK[t] & (1 << slot):
            bp = slot * 41 + 18                      # bit position inside the bundle, bit 0 = LSB of buf[p]
            if getbits(bp + 24, 4) == 5:
                v = getbits(bp, 20); setbits(bp, 20, (v - fo) & 0xFFFFF)
    fo += 1; p += 16
getbits(bp, c): x = u32 LE at buf[p + bp/8]; return (x >> (bp % 8)) & (2^c - 1)
setbits(bp, c, v): read the same u32, replace those c bits with v, write the u32 back
```

**Delta** (LA4:`execute_filter_delta`, RD:`filterDelta`): `ch = R[0]` channels; the input holds each channel's
bytes contiguously.

```
src = 0
for c in 0 .. ch-1:
    prev = 0
    for j = c, c+ch, c+2ch, ... while j < n:
        prev = (prev - buf[src]) & 0xFF; src += 1
        out[j] = prev
```

**RGB** (LA4:`execute_filter_rgb`, RD:`filterRGBV3`): `w = R[0]` (row stride in bytes), `posR = R[1]`. LA4 requires
`n >= 3`, `w <= n`, `posR <= 2`.

```
src = 0
for c in 0..2:
    prev = 0
    for j = c, c+3, c+6, ... while j < n:
        if j >= w:
            up = out[j - w + 3]; upleft = out[j - w]
            pa = |up - upleft|; pb = |prev - upleft|; pc = |up - upleft + prev - upleft|
            pred = prev      if pa <= pb and pa <= pc
                   up        elif pb <= pc
                   upleft    otherwise
        else: pred = prev
        prev = (pred - buf[src]) & 0xFF; src += 1
        out[j] = prev
for i = posR, posR+3, ... while i <= n - 3:
    out[i]   = (out[i]   + out[i+1]) & 0xFF
    out[i+2] = (out[i+2] + out[i+1]) & 0xFF
```

(Byte values are taken as 0..255 integers in the comparisons.)

**Audio** (LA4:`execute_filter_audio`, RD:`filterAudioV3`): `ch = R[0]`.

```
src = 0
for c in 0 .. ch-1:
    W[0..2] = 0 (signed weights); Dl[0..2] = 0; lastDelta = 0; dif[0..6] = 0; count = 0; last = 0
    for j = c, c+ch, ... while j < n:
        Dl[2] = Dl[1]; Dl[1] = lastDelta - Dl[0]; Dl[0] = lastDelta
        pred = ((8*last + W0*Dl0 + W1*Dl1 + W2*Dl2) >> 3) & 0xFF
        e = signed8(buf[src]); src += 1
        b = (pred - e) & 0xFF
        e8 = e << 3
        dif[0] += |e8|
        dif[1] += |e8 - Dl0|; dif[2] += |e8 + Dl0|
        dif[3] += |e8 - Dl1|; dif[4] += |e8 + Dl1|
        dif[5] += |e8 - Dl2|; dif[6] += |e8 + Dl2|
        lastDelta = signed8(b - last); last = b; out[j] = b
        if count & 0x1F == 0:                     # tested before incrementing: at bytes 0, 32, 64, ...
            k = index of the smallest dif[0..6] (first on ties); dif[all] = 0
            k == 1: if W0 >= -16: W0 -= 1     k == 2: if W0 < 16: W0 += 1
            k == 3: if W1 >= -16: W1 -= 1     k == 4: if W1 < 16: W1 += 1
            k == 5: if W2 >= -16: W2 -= 1     k == 6: if W2 < 16: W2 += 1
        count += 1
```

Fixtures: `rar_filter.rar` (`bsdcat.exe`, 204288 bytes: x86 filters). No fixture is known to exercise Itanium,
RGB, Audio or Delta in RAR 3; section 15.

---

## 11. PPMd blocks (RAR 2.9 / 3.x)

Sources: LA4:`parse_codes` (PPMd branch), `read_data_compressed` (escape handling), `ppmd_read`;
RD:`decode29_ppm.go` (`init`, `fill`, `readFilterData`), RD:`ppm_model.go` (`rangeCoder`, `model.init`); PPMD
(`Ppmd7.h`).

The model is PPMd variant H by Dmitry Shkarin, as implemented by the LZMA SDK's `Ppmd7` (`CPpmd7`,
`Ppmd7_Construct`, `Ppmd7_Alloc`, `Ppmd7_Init`). RAR uses it with the **original PPMd range coder**, not 7-Zip's:
in current LZMA SDK versions that pairing is `Ppmd7a_RangeDec_Init` / `Ppmd7a_DecodeSymbol` ("original PPMdH",
per `Ppmd7.h`), not the `Ppmd7z_*` functions. 11.2 specifies the range coder so that it can be written or checked
independently.

### 11.1 PPMd block header

After the block's `ppm` bit (9.1) -- the header is byte-aligned, so the header's first byte holds that bit and these
7 bits:

```
f = read 7 bits
if f & 0x20: mem = next byte                         # model memory = (mem + 1) MiB
if f & 0x40: esc = next byte                         # new escape byte
order = (f & 0x1F) + 1
if order > 16: order = 16 + (order - 16) * 3          # 2..64
if f & 0x20:
    if order == 1: corrupt                            # i.e. f & 0x1F == 0
    allocate the model with (mem + 1) << 20 bytes (Ppmd7_Alloc), Ppmd7_Init(order)
else:
    a model must already exist (from an earlier block of this file or solid stream), else corrupt; keep it as is
initialise the range decoder (11.2): reads 4 bytes
```

The bytes are read through the bit reader (8 bits each); since the header is aligned they are whole bytes. The
escape byte starts at 2 for a non-solid file. When `f & 0x40` is clear, RD keeps the previous escape byte while LA4
resets it to 2: section 15. Do not copy LA4's assignment of the escape byte to `CPpmd7.InitEsc`; that field is
internal to the model, which overwrites it before use.

### 11.2 Range decoder (original PPMd)

From RD:`ppm_model.go:rangeCoder` (all arithmetic unsigned 32-bit, mod 2^32; TOP = 2^24, BOT = 2^15):

```
init:        low = 0; range = 0xFFFFFFFF; code = 0; repeat 4: code = (code << 8) | next byte
get_freq(total):  range = range / total; return (code - low) / range
decode(lo, size): low += range * lo; range *= size; normalize       # size = high - lo
normalize:
    loop:
        if (low ^ (low + range)) >= TOP:
            if range >= BOT: return
            range = (0 - low) & (BOT - 1)
        code = (code << 8) | next byte; range <<= 8; low <<= 8
binary context (one symbol, probability p out of 2^14): count = get_freq(2^14);
                 count < p: the symbol (decode(0, p)); otherwise escape (decode(p, 2^14 - p))
```

The bytes come from the packed stream at the current byte position. The decoder must take **only** the bytes the
range coder asks for: when a PPMd block ends (escape code 0), the next block header starts at the next unread byte.

### 11.3 Escape codes

```
loop:
    c = decode a symbol (Ppmd7a_DecodeSymbol); c < 0 is a data error
    if c != esc: output byte c; continue
    code = decode a symbol
    code 0: end of block: read a new block header (9.1), which may switch to LZ
    code 2: end of file (a following solid file starts with a block header; read it in solid archives, 7.5)
    code 3: filter record (10.1), its bytes read as decoded symbols
    code 4: d = 0; repeat 3: d = (d << 8) | decode a symbol           # 24-bit big-endian
            L = decode a symbol; copy L + 32 bytes from distance d + 2
    code 5: L = decode a symbol; copy L + 4 bytes from distance 1
    other (1 and 6..255): output the byte esc
```

Matches here go through the same window as LZ matches but do not change the LZ distance history or last length.

Fixtures: `rar_compress_best.rar` (METHOD 0x35), `rar_ppmd_lzss_conversion.rar` (switches between PPMd and LZ
blocks; 241 MB output), `rar_ppmd_use_after_free.rar` (fuzzing case; must fail cleanly).

---

## 12. RAR 5 algorithm (compression information version 0, and version 1)

Sources: LA5:`parse_block_header`, `process_block`, `parse_tables`, `create_decode_tables`, `decode_number`,
`do_uncompress_block`, `decode_code_length`, `copy_string`, `dist_cache_push`, `dist_cache_touch`,
`parse_filter`, `parse_filter_data`, `is_valid_filter_block_start`, `apply_filters`, `run_filter`,
`run_delta_filter`, `run_e8e9_filter`, `run_arm_filter`; RD:`decode50.go` (all functions), RD:`filters.go`
(`filterE8`, `filterDelta`, `filterArm`), RD:`decode_reader.go`; TN (compression information).

### 12.1 Compressed blocks

The packed stream of a file is a sequence of blocks, each starting at a byte boundary:

| Byte | Field |
|---|---|
| 0 | flags: bits 0-2 = (number of valid bits in the block's last byte) - 1; bits 3-5 = (number of size bytes) - 1, 0..2; bit 6 = last block of the file; bit 7 = tables present |
| 1 | check byte: `0x5A ^ flags ^ size_byte_0 ^ size_byte_1 ^ size_byte_2` (the size bytes present) |
| 2.. | block size S, 1 to 3 bytes, little-endian |

Bits 3-5 above 2 are invalid (LA5 reads three bits and rejects values above 2; RD reads bits 3-4 and rejects 3;
together: bit 5 must be clear and at most 3 size bytes). A check byte mismatch is a corrupt block. The block body is the
next S bytes; it holds `(S - 1) * 8 + ((flags & 7) + 1)` valid bits, read MSB-first. The next block header follows
the S bytes. If bit 7 is set, the body starts with the tables (12.2), which count toward the body's bits; otherwise
the previous tables stay in force (the first block of a non-solid file must have tables).

Decoding of a block stops when all its valid bits have been consumed (LA5 checks the position before each symbol;
RD limits its bit reader to the block). A symbol that would need bits beyond the block is a data error. After the
block flagged "last", the file's packed stream is complete (in a solid archive, decode up to it even past the
unpacked size: 7.5).

### 12.2 Tables

Precode and run-length codes as in 7.3, values **assigned** (no adding). The lengths, in order:

| Table | Version 0 | Version 1 | Use |
|---|---|---|---|
| main (NC) | 306 | 306 | literals and commands |
| distance (DC) | 64 | 80 | distance slots |
| low distance (LDC) | 16 | 16 | low 4 bits of long distances |
| repeat length (RC) | 44 | 44 | length slots for repeated distances |
| total | 430 | 446 | |

### 12.3 Symbols

State (reset to 0 for a non-solid file, kept for a solid one): `D[0..3]` distances, last length `L`.

```
slot_to_length(s):                       # s from 0 to 43
    if s < 8: return s + 2
    b = s / 4 - 1
    return 2 + ((4 | (s & 3)) << b) + read b bits
```

| Main symbol s | Action |
|---|---|
| 0..255 | literal byte |
| 256 | filter record (12.5) |
| 257 | if L != 0: copy L bytes from distance D[0] (D unchanged) |
| 258..261 | j = s - 258; d = D[j]; move to front (`D[1..j] = D[0..j-1]`, `D[0] = d`); `L = slot_to_length(next RC symbol)`; copy L bytes from d (no length bonus) |
| 262..305 | `len = slot_to_length(s - 262)`; d = distance (below); `len += 1` if d > 0x100, another +1 if d > 0x2000, another +1 if d > 0x40000; push d (`D = [d, D0, D1, D2]`); L = len; copy |

Distance:

```
k = next DC symbol
if k < 4: d = k + 1
else:
    b = k / 2 - 1
    d = 1 + ((2 | (k & 1)) << b)
    if b >= 4:
        if b > 4: d += (read (b - 4) bits) << 4
        d += next LDC symbol                     # 0..15
    else: d += read b bits
```

Note the bonus thresholds compare with `>` here and `>=` in RAR 2.9 (9.4); both sources agree on each. Distances
fit in 32 bits for version 0 (largest slot 63: b = 30); version 1 needs 64-bit distances (12.6).

### 12.4 Window and solid files

Window as in 7.4, size at least the dictionary size from the compression information (LA5 uses exactly that size;
RD at least 256 KiB). In a solid archive, a file with the solid bit continues the window, the tables, `D` and `L`
(RD:`decoder50.init` resets them only when not solid; LA5 keeps the window and tables). Each file's packed stream
begins with a new block header.

### 12.5 Filters

Filter record (main symbol 256), read from the bit stream:

```
fdata(): c = read 2 bits + 1; v = 0; for i in 0..c-1: v |= (read 8 bits) << (8 * i); return v   # little-endian
start  = fdata() + (number of bytes decoded so far in this file)
length = fdata()
type   = read 3 bits
if type == 0 (Delta): channels = read 5 bits + 1
```

| Type | Filter |
|---|---|
| 0 | Delta, `channels` channels (same algorithm as 10.4 Delta) |
| 1 | E8 |
| 2 | E8E9 |
| 3 | ARM |
| 4..7 | invalid in RAR 5 (LA5 names 4..7 Audio, RGB, Itanium, PPM, "not used in RARv5") |

Validity (LA5): `4 <= length <= 0x400000`; a filter must start at or after the end of the previously defined one
(filter ranges do not overlap, so they never chain); at most 8192 pending filters. Filters are applied in order:
output the window bytes before `start` unchanged; once `[start, start + length)` has been fully decoded, output the
filtered copy instead of those bytes. The window keeps the unfiltered data.

The filters, with `off` = start (file offset of buf[0]) and `n = length`:

**E8, E8E9** (LA5:`run_e8e9_filter`, RD:`filterE8` with `v5`): as in 10.4, except that the position is reduced
modulo 2^24: `pos = (off + i + 1) mod 0x1000000`.

**ARM** (LA5:`run_arm_filter`, RD:`filterArm`): ARM BL instructions, absolute-to-relative undone.

```
for i = 0, 4, 8, ... while i + 3 < n:
    if buf[i+3] == 0xEB:
        v = buf[i] | buf[i+1] << 8 | buf[i+2] << 16
        v = (v - (off + i) / 4) & 0xFFFFFF                # integer division
        buf[i], buf[i+1], buf[i+2] = the three bytes of v, little-endian
```

**Delta**: 10.4 with `ch = channels`.

Fixtures: `rar5_arm.rar` (ARM ELF, 90808 bytes), `rar5_multiarchive_solid.part*.rar` (same file, split across
volumes in a solid archive), x86 data in `rar5_compressed.rar`/`rar5_solid.rar` (not confirmed to contain filters).

### 12.6 Version 1 (RAR 7.0)

Sources: TN (compression information), RD:`archive50.go:parseFileHeader`, RD:`decode50.go` (`offsetSize7`,
`tableSize7`). Single-source for the decoding differences; no fixture.

- Algorithm version 1 with bit 20 (0x100000) set: decode exactly as version 0; only the dictionary-size field uses
  the version-1 form.
- Otherwise: the distance table has 80 entries (12.2) and distance slots up to 79 use the same formula as 12.3
  (b up to 38, so distances need 64 bits). Everything else is as in version 0.
- Dictionary size: `128 KiB << N` (N up to 23) plus `(that / 32) * F`, with F the 5-bit fraction (3.5).

---

## 13. Extracting a file: overview

```
open the first volume; read headers (2.7 / 3.9) until a file header
for each file header (first part of a file):
    directories and links: create them; service headers: skip
    packed = concatenation of the data areas of all parts (4.2), read lazily, switching volumes
    if encrypted: packed = AES-CBC-decrypt(packed) with the file's key and IV (6.1 / 6.2)
    if stored: output = packed, cut to the unpacked size
    else: output = decompress(packed) with the algorithm of the header (8, 9-11, 12),
          continuing the solid state if the file is solid (7.5); cut to the unpacked size
          (in a solid archive keep decoding to the end of the file's stream, 7.5)
    verify the CRC / BLAKE2sp of output (5.2, 6.2.5); a mismatch is an error for that file
```

To skip a file in a solid archive, decode it and discard the output. To skip it in a non-solid archive, skip its
data areas.

---

## 14. Limits and robustness

Values from the sources, to apply before allocating or looping:

| Item | Limit | Source |
|---|---|---|
| RAR 4 header size | 7 .. 65535 (u16); file header >= 32 | LA4:`read_header` |
| RAR 5 header size | vint of at most 3 bytes (2 MiB) | TN, LA5, RD (`maxHeaderSize`) |
| RAR 4 dictionary | 64 KiB .. 4 MiB | file flags |
| RAR 5 dictionary | 128 KiB << 15 for version 0; refuse above a configured limit | TN, LA5 (64 MiB), RD (4 GiB default) |
| RAR 5 KDF count | <= 24 | RD `maxKdfCount` |
| RAR 3 program list | 1024 programs | RD `maxUniqueFilters` |
| pending filters | 8192 | RD `maxQueuedFilters`, LA5 deque size |
| RAR 3 program size | 1 .. 0x10000 bytes | LA4, RD |
| RAR 3 global data | <= 0x2000 - 0x40 bytes | LA4, RD |
| RAR 3 filter block | <= 0x3C000 bytes (E8/E8E9), <= 0x1E000 (Delta, RGB, Audio) | LA4 |
| RAR 5 filter block | 4 .. 0x400000 bytes | LA5 |
| RAR 3 VM | 25,000,000 instructions per run | RD `maxCommands` |
| PPMd order | 2 .. 64 | 11.1 |
| PPMd memory | 1 .. 256 MiB | 11.1 |
| password | 128 characters | RD `maxPassword` |

The fuzzing fixtures (`rar_invalid1.rar`, `rar_ppmd_use_after_free.rar`, `rar5_leftshift1.rar`,
`rar5_readtables_overflow.rar`, `rar5_truncated_huff.rar`) must produce clean errors, not crashes or hangs.

---

## 15. Source disagreements and single-source items

Where the sources disagree, the recommended rule is given first. "Fixture-neutral" means the fixtures decode the
same either way (LA4 verifies file CRCs, so every libarchive fixture listed in `expected.txt` decodes correctly
under LA4's behaviour).

| # | Topic | LA | RD | Recommendation |
|---|---|---|---|---|
| 1 | PPMd escape byte when a PPMd block header lacks flag 0x40 (11.1) | LA4 resets it to 2 | keeps the previous value (2 only at a non-solid file start) | Keep the previous value (RD). Fixture-neutral (`rar_ppmd_lzss_conversion.rar`, `rar_compress_best.rar` decode either way); revisit if a PPMd fixture fails |
| 2 | Low-distance repeat state (`lowDist`, `lowRepeat`, 9.4) | LA4 never resets it, not even between files | reset at every LZ table read | Reset at every LZ table read (RD). Test on `rar_multi_lzss_blocks.rar` and `rar_ppmd_lzss_conversion.rar`; if either fails, try "reset only at a non-solid file start" |
| 3 | Distances and last length at a non-solid file start (RAR 2.9, RAR 5) | LA4 keeps them from the previous entry; LA5:`init_unpack` does not reset `dist_cache`/`last_len` either | resets (`lz29Decoder.reset`, `decoder50.init`) | Reset (7.5) |
| 4 | RAR 2.0 distances and last length at a non-solid file start | (no decoder) | not reset | Reset; valid data cannot depend on them |
| 5 | EXT_TIME fraction bytes (2.5) | LA4 accumulates `rem = byte << 16 | rem >> 8` per byte (the formula in 2.5) but then misuses the value | reads the wrong number of bytes for c = 1 and 2 | The formula in 2.5 (LA4's accumulation), consuming exactly c bytes. No oracle holds the time values |
| 6 | Unicode name decoding stop (2.4) | stops at NAME_SIZE output units or at the end of the encoded bytes | stops when the output reaches the narrow name's length or the encoded bytes end | Stop at whichever comes first of: encoded bytes exhausted, output length = narrow length (needed anyway for mode 3 bounds) |
| 7 | Filter-program reset (`vmnum` 0, 10.1) | LA4 also drops pending filters | RD keeps pending filters | Drop programs; keep already pending filters (they hold their own program). Fixture-neutral |
| 8 | RAR 5 time record nanoseconds (3.6) | LA5 reads one u32 | RD reads one per present time | One per present time (TN) |
| 9 | RAR 5 main-header extra records | LA5 rejects any but the locator | RD ignores them | Skip unknown records (TN) |
| 10 | RAR 5 stored CRC of 0 | LA5 does not check it | RD checks | Check whenever flag 0x0004 is set |
| 11 | Unused Huffman code (7.2) | LA5 yields symbol 0; LA4 errors | RD errors | Error |
| 12 | RAR 4 window size | LA4: from the unpacked size, power of two, max 4 MiB, ignoring the header | RD: header dictionary, min 256 KiB | Header dictionary rounded up to a power of two, at least 256 KiB (or always 4 MiB) |
| 13 | Unknown RAR 4 block type | LA4 errors | RD skips | Skip (2.1) |
| 14 | RAR 5 unknown unpacked size | LA5 refuses | RD decodes to the end | Decode to the end (TN) |
| 15 | Escape code 3 inside PPMd (filter record) | LA4 refuses | RD parses it | Parse it (11.3) |
| 16 | RAR 3 filter chaining (10.2) | LA4 compares with a start value it has already cleared, so in effect does not chain | RD chains when the next filter has the same start and the input length matches | Chain (RD); fixture-neutral |
| 17 | Pending filters at a solid file boundary | LA5:`reset_file_context` drops them for every file | RD keeps them when the next file is solid | Keep them (RD): a filter whose block straddles the boundary would otherwise be lost. Fixture-neutral |
| 18 | CRC of skippable RAR 4 blocks with flag 0x8000 (COMM, AV, SUB, PROTECT, SIGN, ENDARC) | LA4 computes it over the header **and** the ADD_SIZE data | RD over the header only | Check over the header only (RD; the rule of 2.1); if that fails, try header + data before reporting damage. Only skippable blocks are affected; no fixture has one |

Single-source items (one source only; no fixture unless stated):

- RAR 2.0 LZ and audio decoding (section 8): RD only; **no fixture**. In particular the RAR 2.0 repeat symbols
  *push* the reused distance (`[d, D0, D1, D2]`) instead of moving it to the front as RAR 2.9 does, and symbol 256
  pushes a copy of D0. These are surprising enough to deserve a test archive (`rar -ma4 -m...` cannot produce
  RAR 2.0 data; an archive from RAR 2.x is needed).
- RAR 3 Itanium filter (10.4) and the VM (Appendix A): RD only; no fixture.
- RAR 4 old-style comment embedded in the main header (2.2): RD only; no fixture.
- RAR 4/5 encryption, header encryption, MAC'd checksums (section 6): RD only, but verified on fixtures.
- RAR 7 (version 1) decoding differences (12.6): RD only; no fixture.
- Volume naming rules (4.1): RD only; the fixtures cover `.partNN.rar` and `.rar/.r00/.r01`.
- BLAKE2sp construction (5.3): LA5 calls a library; the construction comes from the BLAKE2 paper and was verified
  with the reference test vectors only. It has not been checked against `rar5_blake2.rar`, because no fixture holds
  `cebula.txt` uncompressed (it is compressed in `rar5_blake2.rar` and `rar5_multiarchive_solid`, with its CRC
  7e5ec49e in the latter); a decoder that extracts it with the right CRC can then confirm the hash.

---

## Appendix A. The RAR 3 virtual machine (optional)

Source: RD only: `vm.go` (`execute`, the instruction functions, `decodeArg`, `fixJumpOp`, `readCommands`),
`filters.go` (`getV3Filter`, `vmFilter.execute`). LA4 rejects programs that are not one of the standard filters.
No fixture uses a non-standard program. A decoder may omit this appendix and report such filters as unsupported.

### A.1 Program format

The program bytes (10.1) are `code[0..clen-1]`; `code[0]` is the XOR check byte. Read `code[1..]` MSB-first:

```
if read 1 bit: static = (vmnum + 1) bytes, read 8 bits each    # RD requires <= 0x2000 - 0x40
instructions until the bits run out:
    op = read 4 bits
    if op & 8: op = ((op << 2) | read 2 bits) - 24             # 0..7 directly, 8..39 with 6 bits
    if op >= 40: invalid program
    byte_mode = read 1 bit, only for ops marked "b" below
    operands as below
```

| op | name | b | ops | | op | name | b | ops |
|---|---|---|---|---|---|---|---|---|
| 0 | MOV | b | 2 | | 20 | POP | | 1 |
| 1 | CMP | b | 2 | | 21 | CALL | | 1 (jump) |
| 2 | ADD | b | 2 | | 22 | RET | | 0 |
| 3 | SUB | b | 2 | | 23 | NOT | b | 1 |
| 4 | JZ | | 1 (jump) | | 24 | SHL | b | 2 |
| 5 | JNZ | | 1 (jump) | | 25 | SHR | b | 2 |
| 6 | INC | b | 1 | | 26 | SAR | b | 2 |
| 7 | DEC | b | 1 | | 27 | NEG | b | 1 |
| 8 | JMP | | 1 (jump) | | 28 | PUSHA | | 0 |
| 9 | XOR | b | 2 | | 29 | POPA | | 0 |
| 10 | AND | b | 2 | | 30 | PUSHF | | 0 |
| 11 | OR | b | 2 | | 31 | POPF | | 0 |
| 12 | TEST | b | 2 | | 32 | MOVZX | | 2 |
| 13 | JS | | 1 (jump) | | 33 | MOVSX | | 2 |
| 14 | JNS | | 1 (jump) | | 34 | XCHG | b | 2 |
| 15 | JB | | 1 (jump) | | 35 | MUL | b | 2 |
| 16 | JBE | | 1 (jump) | | 36 | DIV | b | 2 |
| 17 | JA | | 1 (jump) | | 37 | ADC | b | 2 |
| 18 | JAE | | 1 (jump) | | 38 | SBB | b | 2 |
| 19 | PUSH | | 1 | | 39 | PRINT | | 0 (no operation) |

Operand encoding:

| Bits | Operand |
|---|---|
| `1 rrr` | register R[r] |
| `0 0` + value | immediate: 8 bits in byte mode, else `vmnum` |
| `0 1 0 rrr` | memory at `R[r] & 0x3FFFF` |
| `0 1 1 0 rrr` + `vmnum` disp | memory at `(R[r] + disp) & 0x3FFFF` |
| `0 1 1 1` + `vmnum` addr | memory at `addr & 0x3FFFF` |

For one-operand jump instructions (JMP, Jcc, CALL) whose operand is an immediate n, the target instruction index is
`n - 256` if n >= 256; otherwise adjust `n -= 264` if n >= 136, `n -= 8` if 16 <= n < 136, `n -= 16` if 8 <= n < 16,
and the target is (index of this instruction + n) mod 2^32. Instruction indices count instructions, not bytes.
Non-immediate jump operands are used as indices directly.

### A.2 Machine

Memory: 0x40000 bytes plus 4 spare bytes (32-bit accesses at 0x3FFFF..0x3FFFC stay in bounds). Registers R0..R7
(32-bit), flags C = 1, Z = 2, S = 0x80000000. Word access is u32 little-endian; byte mode reads one byte (a
register's low 8 bits) and writes one byte (a register's low 8 bits only). `a` is operand 1, `b` operand 2;
"set Z/S" means `fl = Z if r == 0 else r & S`.

| Instruction | Effect |
|---|---|
| MOV | a = b |
| CMP | r = a - b; fl = Z if r == 0 else (C if r > a) \| (r & S) |
| ADD | r = a + b (byte mode: & 0xFF); fl = (C if r < a) \| (Z if r == 0 else S if the sign bit of r is set; sign bit 0x80 in byte mode); a = r |
| SUB | r = a - b; fl as CMP; a = r |
| INC, DEC | r = a + 1 (byte mode & 0xFF) / a - 1; a = r; set Z/S |
| XOR, AND, OR | r = a op b; a = r; set Z/S |
| TEST | r = a & b; set Z/S |
| NOT | a = ~a (no flags) |
| NEG | r = 0 - a; a = r; fl = Z if r == 0 else (r & S) \| C |
| SHL | r = a << b; a = r; set Z/S; C if (a << (b - 1)) has bit 31 |
| SHR | r = a >> b; a = r; set Z/S; C if (a >> (b - 1)) & 1 |
| SAR | r = (signed a) >> b; a = r; set Z/S; C if (a >> (b - 1)) & 1 |
| MUL | a = a * b (mod 2^32); flags unchanged |
| DIV | if b != 0: a = a / b (unsigned); flags unchanged |
| ADC | r = a + b + C (byte mode & 0xFF); a = r; set Z/S; also C if r < a or (r == a and C was set) |
| SBB | r = a - b - C (byte mode & 0xFF); a = r; set Z/S; also C if r > a or (r == a and C was set) |
| MOVZX | a = byte(b) |
| MOVSX | a = sign-extended byte(b) |
| XCHG | swap a and b |
| JMP, JZ (Z), JNZ (not Z), JS (S), JNS (not S), JB (C), JBE (C or Z), JA (neither), JAE (not C) | jump if the condition holds |
| PUSH | R7 -= 4; mem32[R7 & 0x3FFFF] = a |
| POP | a = mem32[R7 & 0x3FFFF]; R7 += 4 |
| CALL | R7 -= 4; mem32[R7 & 0x3FFFF] = index of the next instruction; jump |
| RET | if R7 >= 0x40000: stop; else ip = mem32[R7]; R7 += 4 |
| PUSHA | sp = R7; for R0, R1, ..., R7 in turn: sp = (sp - 4) & 0x3FFFF; mem32[sp] = Ri; then R7 = sp |
| POPA | sp = R7; for i = 7 down to 0: Ri = mem32[sp]; sp = (sp + 4) & 0x3FFFF |
| PUSHF / POPF | push / pop the flags word |

Execution starts at instruction 0 and stops when the instruction index is past the last instruction, on RET with
R7 >= 0x40000, or after 25,000,000 instructions.

### A.3 Running a filter in the VM

```
mem[0 .. n-1] = the block (n <= 0x3C000); the rest zero
R = the registers from the filter record (10.1)
G = mem[0x3C000 .. 0x3E000)                      # global area
G[0x00 .. 0x1B] = R0 .. R6 (u32 each); G[0x1C] = n (u32); G[0x24] = file offset of the block (u64)
G[0x2C] = number of earlier runs of this program (u32)
G[0x40 ..] = this program's saved global data if it has any, else the record's global data; then its static data
R6 = file offset of the block (low 32 bits)
run
out_len = G[0x1C] & 0x3FFFF; out_start = G[0x20] & 0x3FFFF
if out_start + out_len > 0x40000: output nothing; else output mem[out_start .. out_start + out_len)
if u32 G[0x30] > 0: save min(G[0x30], 0x2000 - 0x40) bytes of G[0x40 ..] as this program's saved global data
```

---

## Appendix B. Test fixtures

All in `tests/fixtures/rar/` (see its `README.md`). Oracles, one line per extracted file as
`archive|entry|size|crc32`: `expected.txt` (libarchive 3.7.7's output, for the libarchive fixtures) and
`expected-unrar.txt` (UnRAR 7.3.1's output, for the fixtures made for this project; first volume named for
volume sets). Passwords: `pass` for the project's `rar4_*`/`rar5_*` encrypted fixtures, U+BE44 U+BC00 U+0020 U+BC88
U+D638 for `*_enc_hangul` (encodings in 6.1.2 and 6.2.5), `password` for `rar5_encrypted_filenames.rar` and
`rar5_encrypted.rar`'s `b.txt`; unknown for `rar4_encrypted.rar`, `rar_encryption_header.rar` and
`rar5_encrypted.rar`'s `d.txt`.

| Fixture | What it holds | Sections |
|---|---|---|
| `rar.rar` | RAR 4, Unix host, stored files, a symlink `testlink`, directories, `\` separators | 2.3-2.7 |
| `rar_windows.rar` | RAR 4, Win32 host, stored files, directories | 2.3, 2.5 |
| `rar_unicode.rar` | RAR 4 names with Shift-JIS narrow part + encoded UTF-16; one LZ-compressed file | 2.4, 9 |
| `rar_noeof.rar` | RAR 4 without end-of-archive header | 2.6 |
| `rar_subblock.rar` | RAR 4 NEWSUB `CMT` service header (compressed) before the file | 2.1, 2.3 |
| `rar_compress_normal.rar` | RAR 2.9 LZ (METHOD 0x33) | 7, 9 |
| `rar_compress_best.rar` | RAR 2.9 PPMd (METHOD 0x35) | 11 |
| `rar_multi_lzss_blocks.rar` | RAR 2.9 LZ, many blocks and table changes, 20 MB output, 4 MiB dictionary | 9, 15 #2 |
| `rar_ppmd_lzss_conversion.rar` | RAR 2.9 switching between PPMd and LZ blocks, 241 MB output | 9, 11, 15 #1-2 |
| `rar_filter.rar` | RAR 2.9 with x86 filters (`bsdcat.exe`) | 10 |
| `rar_invalid1.rar`, `rar_ppmd_use_after_free.rar` | corrupt RAR 4 (fuzzing); must fail cleanly | 14 |
| `rar4_enc_data.rar` | RAR 4, data encrypted (`-p`), LZ, Unicode name with UTF-8 narrow part | 6.1.1, 6.1.2, 2.4 |
| `rar4_enc_headers.rar` | RAR 4, headers encrypted (`-hp`) | 6.1.3 |
| `rar4_enc_solid.rar` | RAR 4 solid, encrypted | 6.1.2, 7.5 |
| `rar4_enc_stored.rar` | RAR 4 stored, encrypted (padding beyond the unpacked size) | 6.1.2 |
| `rar4_enc_hangul.rar` | RAR 4 encrypted with a Hangul password | 6.1.1 |
| `rar4_encrypted.rar` | libarchive's: two of four files encrypted, password unknown | 6.1 (detection) |
| `rar_encryption_header.rar` | libarchive's: encrypted headers, password unknown | 6.1.3 (detection) |
| `rar4_vol.part01..03.rar` | RAR 4 volumes, new naming, files split across volumes | 4 |
| `rar4_oldvol.rar`, `.r00`, `.r01` | RAR 4 volumes, old naming | 4.1 |
| `rar4_vol_enc.part01..03.rar` | RAR 4 volumes, solid, encrypted headers and data, one CBC chain across parts | 4.2, 6.1.3, 7.5 |
| `rar5_stored.rar` | RAR 5, stored file | 3 |
| `rar5_compressed.rar` | RAR 5, compressed file | 12 |
| `rar5_multiple_files.rar` | RAR 5, four compressed files | 12 |
| `rar5_multiple_files_solid.rar`, `rar5_solid.rar` | RAR 5 solid | 12.4, 7.5 |
| `rar5_win32.rar` | RAR 5, Windows host, FILETIME time records | 3.6, 3.7 |
| `rar5_fileattr.rar` | RAR 5, Windows attributes (read-only, hidden, system, directories) | 3.7 |
| `rar5_blake2.rar` | RAR 5 with a BLAKE2sp hash record instead of a CRC | 5.3 |
| `rar5_arm.rar` | RAR 5, ARM executable (ARM filter) | 12.5 |
| `rar5_multiarchive_solid.part01..04.rar` | RAR 5 solid volume set; one file split over four volumes; blocks straddle volumes | 4.2, 12 |
| `rar5_vol.part01..03.rar` | RAR 5 volumes | 4 |
| `rar5_vol_enc.part01..03.rar` | RAR 5 volumes, solid, encrypted headers and data | 4, 6.2.4 |
| `rar5_enc_data.rar` | RAR 5 encrypted data with MAC'd CRCs | 6.2.3, 6.2.5 |
| `rar5_enc_headers.rar` | RAR 5 encrypted headers | 3.3, 6.2.4 |
| `rar5_enc_solid.rar` | RAR 5 solid, encrypted | 6.2.3, 12.4 |
| `rar5_enc_stored.rar` | RAR 5 stored, encrypted | 6.2.3 |
| `rar5_enc_hangul.rar` | RAR 5 encrypted with a Hangul password | 6.2.1 |
| `rar5_encrypted.rar` | libarchive's: two of four files encrypted (one with `password`) | 6.2 |
| `rar5_encrypted_filenames.rar` | libarchive's: encrypted headers, password `password` | 6.2.4 |
| `rar5_leftshift1.rar`, `rar5_readtables_overflow.rar`, `rar5_truncated_huff.rar` | corrupt RAR 5 (fuzzing); must fail cleanly | 14 |

Not covered by any fixture: SFX archives; RAR 2.0 compressed data (LZ and audio); RAR 3 Itanium, RGB, Audio and
Delta filters (not confirmed) and non-standard VM programs; filter records inside PPMd; RAR 5 Delta filter (not
confirmed); RAR 7 (version 1) data; RAR 4 large-file (flag 0x0100) headers; RAR 4 old-style comments; RAR 5
unknown unpacked size; BLAKE2sp combined with MAC; RAR 5 redirection (link) records.
