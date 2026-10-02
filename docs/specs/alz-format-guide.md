# The ALZ archive format: a complete guide for implementers

A guide to reading ALZ archives (`.alz`, with split volumes `.a00`, `.a01`, ...), written so that someone who
has never written a file-format parser can build a working reader from it. It explains the background
first (bytes, little-endian numbers, bit streams, CRC-32, MS-DOS times, code pages), then every
structure in the file, byte by byte, with tables, diagrams and hex dumps you can check by hand, and
ends with a full reading algorithm, the pitfalls, and how to test.

Written 2026-10-02 by the clean-room `alz-decoder` session of Rubraview. Licence: the project's (MIT).

## Contents

- [0. Where the facts come from](#0-where-the-facts-come-from)
- [1. Background you need](#1-background-you-need)
- [2. The big picture](#2-the-big-picture)
- [3. The archive header](#3-the-archive-header)
- [4. The local file header](#4-the-local-file-header)
- [5. The central directory record and the end record](#5-the-central-directory-record-and-the-end-record)
- [6. Walking the archive: the listing algorithm](#6-walking-the-archive-the-listing-algorithm)
- [7. Split archives (volumes)](#7-split-archives-volumes)
- [8. Compression methods](#8-compression-methods)
- [9. Encryption](#9-encryption)
- [10. Reading one file: the whole pipeline](#10-reading-one-file-the-whole-pipeline)
- [11. Safety: hostile and damaged input](#11-safety-hostile-and-damaged-input)
- [12. A complete reader in pseudo-code](#12-a-complete-reader-in-pseudo-code)
- [13. Testing your reader](#13-testing-your-reader)
- [14. What ALZip versions actually write](#14-what-alzip-versions-actually-write)
- [15. Open points](#15-open-points)
- [16. Glossary](#16-glossary)

---

## 0. Where the facts come from

Every statement in this guide comes from one of three places, and the difference matters when your reader
meets a file nobody has seen before:

| Mark | Source | How sure |
|---|---|---|
| (spec) | `docs/specs/alz-format.md`, the project's format specification | the description the reader was built from |
| (ALZip) | archives made by ESTsoft's ALZip 4.9, 5.03, 6.7, 8.12 and 12.37, run as black boxes (files in, archive out) and decoded file for file against their inputs | confirmed on real output |
| (std) | public standards: RFC 1951 (deflate), bzip2's own documentation and source, PKWARE's APPNOTE ("Traditional PKWARE Encryption") | standard algorithms ALZ reuses unchanged |

Where real ALZip output disagrees with the spec, this guide says so and follows the real output. ESTsoft
has published no specification of ALZ; fields nobody has explained are marked *unknown*: skip them, never
trust them.

The test archives mentioned throughout are in `tests/fixtures/alz/` (their origins are in its `README.md`), and
the reference reader is `src/core/alz.c` with `include/rubraview/alz.h`.

---

## 1. Background you need

Skip any part you already know.

### 1.1 Bytes and hexadecimal

A file is a sequence of **bytes**; a byte holds a number from 0 to 255. Formats are always discussed in
**hexadecimal** (base 16), where one byte is exactly two digits `00`..`FF`. This guide writes hex bytes as
`4C` or `0x4C`.

A **hex dump** shows a file 16 bytes per line: the offset (position from the start, in hex), the bytes, and
the same bytes as text (`.` for bytes that are not printable letters):

```
offset    bytes (hex)                                       as text
00000000  41 4c 5a 01 0a 00 00 00 42 4c 5a 01 05 00 20 71  |ALZ.....BLZ... q|
```

`41 4C 5A` are the ASCII codes of `A`, `L`, `Z`. To make your own dumps: `xxd file`, `hexdump -C file`,
or `od -A x -t x1z file` on Linux and macOS; `Format-Hex file` in PowerShell.

### 1.2 Numbers wider than a byte: little-endian

ALZ stores every multi-byte number **little-endian**: the *least* significant byte comes *first*.

```
the 4 bytes in the file:   90 D7 09 68
read little-endian:        0x6809D790   (the last byte is the most significant)

value = b[0] + b[1]*256 + b[2]*256^2 + b[3]*256^3
      = 0x90 + 0xD7*0x100 + 0x09*0x10000 + 0x68*0x1000000
```

In C, read them byte by byte, never by casting a pointer (that breaks on unaligned data and big-endian
machines):

```c
uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* n = 1, 2, 4 or 8 bytes, as ALZ's size fields are */
uint64_t le_n(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = n; i-- > 0;) v = (v << 8) | p[i];
    return v;
}
```

### 1.3 Signatures (magic numbers)

Many formats mark each structure with fixed bytes, a **signature**, so that a reader can recognise it. ALZ
uses four-byte signatures that spell `ALZ`, `BLZ`, `CLZ` and `DLZ` followed by a small number:

| Bytes | Text | Meaning |
|---|---|---|
| `41 4C 5A 01` | `ALZ` 01 | archive header (start of the file, and of every volume) |
| `42 4C 5A 01` | `BLZ` 01 | a file or folder entry |
| `43 4C 5A 01` | `CLZ` 01 | central directory record |
| `43 4C 5A 02` | `CLZ` 02 | end of archive |
| `43 4C 5A 03` | `CLZ` 03 | end of a volume that is not the last (ALZip) |
| `44 4C 5A 01` | `DLZ` 01 | start of a block inside ALZ-bzip2 data |
| `44 4C 5A 02` | `DLZ` 02 | end of ALZ-bzip2 data |

Decide what a file is from its first bytes, not from its name: a reader shown `ALZ` 01 at offset 0 has an ALZ
archive, whatever the extension says.

### 1.4 Bits, and reading a bit stream

A byte is 8 **bits**. Compressed data is usually a **bit stream**: values of any width (1 bit, 3 bits,
24 bits, ...) packed one after another with no regard for byte boundaries. Two conventions exist; bzip2
(and so ALZ-bzip2) reads **most significant bit first**:

```
bytes:            0xB5                0x80
bits:          1 0 1 1 0 1 0 1     1 0 0 0 0 0 0 0
               ^ first bit read

bits(3) -> 101 = 5     bits(4) -> 1010 = 10      (the 4 bits cross into the second byte)
```

A minimal MSB-first bit reader:

```c
typedef struct { const uint8_t *p; size_t len, pos; uint32_t buf; int have; } bitreader;

/* k = 1..24; returns -1 when the data runs out */
int32_t bits(bitreader *r, int k) {
    while (r->have < k) {
        if (r->pos == r->len) return -1;
        r->buf = (r->buf << 8) | r->p[r->pos++];
        r->have += 8;
    }
    r->have -= k;
    return (int32_t)((r->buf >> r->have) & ((1u << k) - 1));
}
```

Deflate (method 2) uses the other convention, least significant bit first. You will not write that reader
yourself: use a deflate library (section 8.2).

### 1.5 CRC-32

A **CRC** (cyclic redundancy check) is a checksum: a 32-bit number computed from the data, stored when the
archive is made and recomputed when it is read. If they differ, the data is damaged (or, for an encrypted
file, the password was wrong). ALZ uses the same CRC-32 as ZIP, gzip, PNG and zlib: reflected polynomial
`0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`.

```c
uint32_t crc_table[256];
void crc_init(void) {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
```

Check values to test yours: `crc32("abc") = 0x352441C2`, `crc32("Hello") = 0xF7D18982`, `crc32("") = 0`.
zlib's `crc32()` and miniz's `mz_crc32()` compute exactly this.

The **table** and the single **step** `table[(k ^ b) & 0xFF] ^ (k >> 8)` reappear in the encryption
(section 9), there *without* the initial and final inversion.

### 1.6 MS-DOS date and time

Each entry stores its modification time in the 32-bit MS-DOS format: the date in the high 16 bits, the time
in the low 16 bits, local time, 2-second resolution.

```
 31        25 24    21 20      16 15     11 10        5 4       0
+------------+--------+----------+---------+-----------+---------+
| year-1980  | month  |   day    |  hour   |  minute   | sec/2   |
|  7 bits    | 4 bits |  5 bits  | 5 bits  |  6 bits   | 5 bits  |
+------------+--------+----------+---------+-----------+---------+
```

Worked example, `0x579D6971` (from the archives in this guide):

| Part | Bits | Value |
|---|---|---|
| date = `0x579D` | year = `0x579D >> 9` = 43 | 1980 + 43 = **2023** |
| | month = `(0x579D >> 5) & 15` | **12** |
| | day = `0x579D & 31` | **29** |
| time = `0x6971` | hour = `0x6971 >> 11` | **13** |
| | minute = `(0x6971 >> 5) & 63` | **11** |
| | second = `(0x6971 & 31) * 2` | **34** |

So the time is 2023-12-29 13:11:34.

### 1.7 File names, code pages and why they are bytes

ALZ stores a file's name as **raw bytes in the code page of the computer that made the archive**, with no field
saying which one. ALZip is Korean, so in practice it is **CP949** (Windows' Korean code page, a superset of
EUC-KR). ASCII characters are one byte each; a Hangul syllable is two bytes:

| Text | Bytes in CP949 | Bytes in UTF-8 |
|---|---|---|
| `a.txt` | `61 2E 74 78 74` | `61 2E 74 78 74` (the same) |
| `페이지` | `C6 E4 C0 CC C1 F6` | `ED 8E 98 EC 9D B4 EC A7 80` |
| `제1권` | `C1 A6 31 B1 C7` | `EC A0 9C 31 EA B6 8C` |
| `비밀` | `BA F1 B9 D0` | `EB B9 84 EB B0 80` |

Rules for a reader:

- Keep the name as bytes while you parse. Convert it for display afterwards, with CP949 as the default and a
  way for the user to choose another code page (Japanese Shift-JIS archives exist). On Windows,
  `MultiByteToWideChar(949, ...)` converts; elsewhere iconv or ICU.
- Folder separators: the spec says `\`; every ALZip version tested (4.9 to 12.37) writes `/` (ALZip). Accept both.
  Replace `\` with `/` only **after** converting to Unicode: in Shift-JIS the byte `0x5C` (`\`) can be the
  second half of a character. (In CP949 it never is: the second byte of a Hangul pair is `0x41`..`0xFE`
  and never `0x5C`, `0x2F` `/`, `0x2E` `.` or `0x3A` `:`.)

### 1.8 Little vocabulary

- **Offset**: a position in the file, counted in bytes from the start (0-based).
- **Record**: one structure in the file, starting with a signature.
- **Entry**: one file or folder stored in the archive.
- **Packed** (or compressed) size: how many bytes the entry's data takes in the archive. **Unpacked** (or
  uncompressed) size: how big the file is once decoded.

---

## 2. The big picture

An ALZ archive is a flat sequence of records. There is no index at the end that you must read first (unlike
ZIP): you read from the start, one record after another.

```
offset 0
+------------------------------+
| "ALZ" 01  archive header     |  8 bytes                          (section 3)
+------------------------------+
| "BLZ" 01  local file header  |  13+ bytes: sizes, CRC, name ...  (section 4)
|           [12-byte crypt hdr]|  only if the entry is encrypted   (section 9)
|           file data          |  packed size bytes                (section 8)
+------------------------------+
| "BLZ" 01  local file header  |
|           file data          |
+------------------------------+
|   ... one per file/folder    |
+------------------------------+
| "CLZ" 01  central directory  |  8 bytes after the signature      (section 5)
+------------------------------+
| "CLZ" 02  end of archive     |  4 bytes, nothing after
+------------------------------+
```

A split archive cuts this same sequence into pieces (`.alz`, `.a00`, `.a01`, ...), each with a small head and
tail of its own (section 7). Glue the pieces back together and you have exactly the picture above.

### 2.1 A whole archive you can decode by hand

This is a complete, valid 55-byte archive holding one file, `a.txt`, containing `Hello`, stored
(uncompressed). Every byte is explained below it; work through it once and the rest of the guide is detail.

```
00000000  41 4c 5a 01 0a 00 00 00 42 4c 5a 01 05 00 20 71  |ALZ.....BLZ... q|
00000010  69 9d 57 10 00 00 00 82 89 d1 f7 05 05 61 2e 74  |i.W..........a.t|
00000020  78 74 48 65 6c 6c 6f 43 4c 5a 01 00 00 00 00 00  |xtHelloCLZ......|
00000030  00 00 00 43 4c 5a 02                             |...CLZ.|
```

| Offset | Bytes | Field | Value |
|---|---|---|---|
| `00` | `41 4C 5A 01` | archive signature | `ALZ` 01 |
| `04` | `0A 00 00 00` | archive header fields | `0A 00` (unknown, always this) + volume number 0 |
| `08` | `42 4C 5A 01` | entry signature | `BLZ` 01 |
| `0C` | `05 00` | name length | 5 |
| `0E` | `20` | attributes | `0x20` = an ordinary file |
| `0F` | `71 69 9D 57` | MS-DOS time | `0x579D6971` = 2023-12-29 13:11:34 |
| `13` | `10` | descriptor | width 1 (`0x10 >> 4`), not encrypted |
| `14` | `00` | unknown | 0 |
| `15` | `00` | method | 0 = stored |
| `16` | `00` | unknown | 0 |
| `17` | `82 89 D1 F7` | CRC-32 | `0xF7D18982` = crc32("Hello") |
| `1B` | `05` | packed size (1 byte) | 5 |
| `1C` | `05` | unpacked size (1 byte) | 5 |
| `1D` | `61 2E 74 78 74` | name | `a.txt` |
| `22` | `48 65 6C 6C 6F` | data | `Hello` |
| `27` | `43 4C 5A 01` | central directory signature | `CLZ` 01 |
| `2B` | `00` x 8 | central directory fields | skipped |
| `33` | `43 4C 5A 02` | end signature | `CLZ` 02 |

---

## 3. The archive header

```
+----+----+----+----+----+----+----+----+
| 41 | 4C | 5A | 01 | 0A | 00 | vol number |
+----+----+----+----+----+----+----+----+
  "A"  "L"  "Z"  01   unknown    LE16
```

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | signature `41 4C 5A 01` | required: without it, it is not an ALZ archive |
| 4 | 2 | unknown | `0A 00` in every archive seen (perhaps a format version, 10) |
| 6 | 2 | volume number, little-endian | 0 in a `.alz`; 1 in `.a00`, 2 in `.a01`, ... (ALZip; see 7.3) |

The spec calls all 4 bytes after the signature *unknown* and says to skip them. Skip them for parsing; the
volume number is only useful when you look for volumes (section 7).

---

## 4. The local file header

One per file or folder, immediately followed by the entry's data. It has a fixed part, an optional part whose
size depends on the descriptor, the name, the optional encryption header, and the data.

```
 "BLZ" 01 | fixed part (9)  | [method..sizes: 6 + 2w] | name (n) | [crypt hdr (12)] | data (c)
+---------+-----------------+-------------------------+----------+------------------+----------+
| 4 bytes | n attr time d u | m u crc   c   u         |  bytes   |  if encrypted    |  bytes   |
+---------+-----------------+-------------------------+----------+------------------+----------+
            only if w != 0 ---^                                     not counted in c
```

### 4.1 Fixed part (9 bytes after the signature)

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | name length `n` (LE16) |
| 2 | 1 | attributes |
| 3 | 4 | modification time, MS-DOS format (section 1.6) |
| 7 | 1 | descriptor (below) |
| 8 | 1 | unknown (0 in every archive seen) |

**Attributes** (Windows file attribute bits):

| Bit | Meaning |
|---|---|
| `0x01` | read-only |
| `0x02` | hidden |
| `0x10` | folder: the entry is a directory and has no data |
| `0x20` | file ("archive" bit); every ordinary file in ALZip's archives has it |

**Descriptor**:

```
  bit:   7   6   5   4   3   2   1   0
       +---------------+---+---+---+---+
       |  width w      | D | ? | ? | E |
       +---------------+---+---+---+---+
         w = descriptor >> 4: 0, 1, 2, 4 or 8
         D = 0x08: "data descriptor" flag, changes the password check (9.4)
         E = 0x01: the entry is encrypted
```

| Descriptor | Meaning | Seen in |
|---|---|---|
| `0x00` | no sizes (empty file or folder), not encrypted | all versions, for empty files |
| `0x10` / `0x20` / `0x40` / `0x80` | sizes of 1 / 2 / 4 / 8 bytes | ALZip writes 2 for files under 64 KB and 4 above; 1 and 8 are valid but were not seen |
| `0x11` / `0x21` / `0x41` / `0x81` | the same, encrypted | ALZip with a password |
| `0x?9` | encrypted with flag `0x08` | in the spec; **never** written by any ALZip version tested |

Any width other than 0, 1, 2, 4, 8 means the header is damaged: stop reading there.

### 4.2 Method, CRC and sizes (only when w != 0)

| Size | Field |
|---|---|
| 1 | compression method: 0 stored, 1 ALZ-bzip2, 2 raw deflate |
| 1 | unknown (0) |
| 4 | CRC-32 of the **unpacked** data (LE32) |
| w | packed size `c` (LE, `w` bytes) |
| w | unpacked size `u` (LE, `w` bytes) |

When `w = 0` there are no such fields: method, CRC and both sizes are 0.

### 4.3 Name, encryption header, data

| Size | Field |
|---|---|
| n | the name, bytes in the maker's code page (section 1.7) |
| 12 | only if encrypted: the encryption header (section 9.3) |
| c | the data: `c` bytes, encrypted if the entry is |

**`c` does not include the 12-byte encryption header.** The next record's signature follows the data directly.

### 4.4 A real header from ALZip

The first entry of `defaults.alz` (EggDotNet's test archive, made by ALZip):

```
00000008  42 4c 5a 01 14 00 20 71 69 9d 57 40 00 02 00 90  BLZ.......
00000018  d7 09 68 02 e0 11 00 48 3c 12 00 6c 6f 72 65 6d  ...  lorem
```

| Bytes | Field | Value |
|---|---|---|
| `42 4C 5A 01` | signature | `BLZ` 01 |
| `14 00` | name length | 20 |
| `20` | attributes | file |
| `71 69 9D 57` | time | 2023-12-29 13:11:34 |
| `40` | descriptor | width 4, not encrypted |
| `00` | unknown | |
| `02` | method | raw deflate |
| `00` | unknown | |
| `90 D7 09 68` | CRC-32 | `0x6809D790` |
| `02 E0 11 00` | packed size | 1 171 458 |
| `48 3C 12 00` | unpacked size | 1 195 080 |
| `6C 6F 72 ...` | name (20 bytes) | `lorem_ipsum_long.tif` |

The deflated data (1 171 458 bytes) follows the name.

---

## 5. The central directory record and the end record

After the last entry come two short records.

```
+-----------+--------------------------------------+     +-----------+
| "CLZ" 01  |  8 bytes (ALZip)  / 12 (spec)        | ... | "CLZ" 02  |
+-----------+--------------------------------------+     +-----------+
```

| Record | After the signature | Notes |
|---|---|---|
| `CLZ` 01, central directory | **8 bytes** in every archive ALZip made; the spec says 12 | ALZip 5.03 and later: 8 zeros. ALZip 4.9: 4 zeros then 4 non-zero bytes (perhaps a CRC) |
| `CLZ` 02, end of archive | nothing | the last 4 bytes of the archive (or of its last volume) |

Despite its name, the "central directory" holds **no file list**: the local headers are the only list.
Because the spec and the real files disagree on its size, the safe rule is: **when you meet `CLZ` 01 or
`CLZ` 02, the list of files is complete; stop.** You never need to read past `CLZ` 01.

---

## 6. Walking the archive: the listing algorithm

### 6.1 The loop

```
check that the data starts with "ALZ" 01; if not: not an ALZ archive
pos = 8
loop:
    if fewer than 4 bytes are left:                 stop (damaged: no end record)
    sig = 4 bytes at pos; pos += 4
    if sig is "CLZ" 01 or "CLZ" 02:                 stop (complete)
    if sig is not "BLZ" 01:                         stop (damaged from here)
    read 9 bytes: n, attributes, time, descriptor, unknown     (stop if missing)
    w = descriptor >> 4;  if w not in {0,1,2,4,8}:  stop (damaged)
    if w != 0: read method, unknown, crc, c (w bytes), u (w bytes)   (stop if missing)
    read n bytes of name                                            (stop if missing)
    if descriptor & 1: pos += 12          (the encryption header)
    data_offset = pos
    if c > bytes left after pos:          the entry is cut off: remember it as truncated, stop
    record the entry (unless it is a folder, or its name is unsafe: 11.2)
    pos += c
```

### 6.2 As a state diagram

```
            +----------------+
  start --> | "ALZ" 01 ?     |--no--> not an ALZ archive
            +----------------+
                    | yes, skip 8
                    v
            +----------------+  "CLZ" 01 / 02   +----------+
     +----> | read signature |----------------->| complete |
     |      +----------------+                  +----------+
     |              | "BLZ" 01        anything else / too short
     |              v                        |
     |      +----------------+               v
     |      | read header,   |-- bad ---> +-------------------------+
     |      | name, [crypt]  |            | damaged: keep the list  |
     |      +----------------+            | read so far             |
     |              |                     +-------------------------+
     |              v                        ^
     |      +----------------+  data cut     |
     |      | skip c bytes   |---------------+
     |      +----------------+
     |              |
     +--------------+
```

### 6.3 Damage is normal

Files get truncated by downloads, volumes go missing. The format has no index, so whatever was listed before
the damage is still valid: keep it, and let the user read those files. A file whose data is cut off cannot
pass its CRC check; do not offer it as readable (or offer only "partial", if your program can use that).

---

## 7. Split archives (volumes)

### 7.1 Names

A split archive is a set of files named after the first:

| Volume index | File | Rule |
|---|---|---|
| 0 | `name.alz` | the first |
| 1 | `name.a00` | `x = 'a' + (i-1)/100`, digits `(i-1) % 100` |
| 2 | `name.a01` | |
| ... | ... | |
| 100 | `name.a99` | |
| 101 | `name.b00` | after `a99` comes `b00` (ALZip's help says so too) |

Volumes are found by name, in this order, until one is missing. Keep the letter's case: `X.ALZ` is followed
by `X.A00`. ALZip 12.37 and 6.7 refuse volumes smaller than 64 KB (65 536 bytes); ALZip 4.9's dialog offers 1 457 664
(a floppy), 2 MB and 500 KB, or a size typed in.

### 7.2 Joining volumes into one stream

Every volume except the first starts with an **8-byte head**; every volume except the last ends with a
**16-byte tail**. Cut those off and concatenate what is left: the result is the archive of section 2.
Records and file data cross volume boundaries anywhere, even in the middle of a header.

```
name.alz                    name.a00                     name.a01 (last)
+---------------------+--+  +--+----------------------+--+  +--+------------------+
|  part 0             |T |  |H |  part 1              |T |  |H |  part 2          |
+---------------------+--+  +--+----------------------+--+  +--+------------------+
                       16     8                         16    8

joined:  [ part 0 ][ part 1 ][ part 2 ]   = "ALZ" 01 ... "BLZ" 01 ... "CLZ" 02
```

Volume `i` contributes its bytes `[h_i, size_i - t_i)` with `h_0 = 0`, `h_i = 8` for `i >= 1`, `t_i = 16`
except `t_last = 0`.

To read at logical offset `x`, walk the volumes:

```c
/* where logical offset x is: volume index and offset inside that volume */
for (i = 0; i < count; ++i) {
    head = i > 0 ? 8 : 0;
    tail = i + 1 < count ? 16 : 0;
    len  = size[i] > head + tail ? size[i] - head - tail : 0;
    if (x < len) return (i, head + x);      /* found */
    x -= len;
}
return not_there;                           /* past the end: the data is cut off */
```

Worked example with the test set `a49_vol.alz` (100 000 bytes), `.a00` (100 000), `.a01` (95 286):
the parts are 99 984, 99 976 and 95 278 bytes, together 295 238, which is exactly the size of the same
archive made unsplit (`a49_max.alz`). Logical offset 150 000 is in `.a00` (150 000 - 99 984 = 50 016 into
its part), at file offset 8 + 50 016 = 50 024.

### 7.3 What the head and tail contain (ALZip)

The spec leaves them undescribed; ALZip 4.9, 6.7 and 12.37 all write:

```
head (8 bytes), every volume after the first:
  41 4C 5A 01   0A 00   NN NN
  "ALZ" 01      unknown volume number, LE16 (1 for .a00, 2 for .a01, ...)

tail (16 bytes), every volume but the last:
  43 4C 5A 01   xx xx xx xx xx xx xx xx   43 4C 5A 03
  "CLZ" 01      8 bytes (zeros; ALZip 4.9: 4 zeros + 4 others)   "CLZ" 03
```

Note that the head is exactly the archive header of section 3: the `.alz` is volume 0. These facts are useful
when you look for volumes by name:

- If the `.alz` ends with `CLZ` 02 (and not `CLZ` 03), it is a whole archive: do not join a `.a00` that
  happens to lie beside it.
- If a candidate volume starts with `ALZ` 01 but its number is not the one expected, it belongs to another
  set: stop there.
- Stop after the volume that ends with `CLZ` 02.

Treat a head that is not `ALZ` 01 as the spec does (skip 8 bytes, use the volume): the spec does not promise
the contents. The reference reader applies these rules only when finding volumes, never while decoding.

---

## 8. Compression methods

| Method | Name | Data is | Written by |
|---|---|---|---|
| 0 | stored | the file itself; `c` must equal `u` | "no compression" in every version; also every empty file |
| 1 | ALZ-bzip2 | bzip2 with its framing cut down | ALZip 4.9 (its "maximum", the only ALZ choice besides "no compression") |
| 2 | deflate | raw deflate (RFC 1951) | ALZip 5.03, 6.7, 8.12 and 12.37 (every compressing level tested) |
| other | unknown | | treat as unsupported; list the file, refuse to read it |

### 8.1 Method 0: stored

Copy `c` bytes. If `c != u` the header is wrong; refuse.

### 8.2 Method 2: raw deflate

**Deflate** (RFC 1951) is the LZ77 + Huffman compression of ZIP, gzip and PNG. It comes in three wrappings,
and ALZ uses the barest one:

| Wrapping | Starts with | Used by |
|---|---|---|
| raw deflate | the first deflate block, no header | **ALZ**, ZIP |
| zlib (RFC 1950) | 2-byte header (`78 9C`, ...), ends with Adler-32 | PNG, many libraries' default |
| gzip (RFC 1952) | `1F 8B ...` | `.gz` files |

Do not write an inflater yourself; every language has one. Ask it for **raw** deflate:

| Library | How to ask for raw deflate |
|---|---|
| zlib (C) | `inflateInit2(&strm, -15)` (negative window bits = raw) |
| miniz (C) | `tinfl_decompress` without `TINFL_FLAG_PARSE_ZLIB_HEADER` |
| Python | `zlib.decompressobj(-15)` |
| Java | `new Inflater(true)` (nowrap) |
| .NET | `DeflateStream` (it is raw) |
| Go | `compress/flate.NewReader` |
| Rust | `flate2::read::DeflateDecoder` |

Example: `Hello` deflated raw is the 7 bytes `F3 48 CD C9 C9 07 00`.

Decode until you have `u` bytes or the stream ends. Give the inflater an output buffer of exactly `u` bytes:
a stream that tries to produce more is broken (or hostile), and stopping at `u` protects you. Then check the
CRC-32.

### 8.3 Method 1: ALZ-bzip2

#### What bzip2 is

bzip2 compresses data in **blocks** of up to 900 000 bytes, each independently, through a pipeline:

```
compress:   RLE1 -> BWT -> MTF -> RLE2 -> Huffman         (per block)
decompress: Huffman -> RLE2 -> MTF -> inverse BWT -> RLE1
```

- **RLE1**: runs of 4 to 255 equal bytes become 4 bytes + a count.
- **BWT** (Burrows-Wheeler transform): sorts all rotations of the block; the last column clusters equal
  letters together. Storing that column plus the row of the original (the **origPtr**, 24 bits) is enough to
  undo it.
- **MTF** (move to front): turns the clustered letters into mostly small numbers.
- **RLE2**: codes runs of zeros with the special symbols RUNA and RUNB.
- **Huffman**: 2 to 6 code tables, switching every 50 symbols ("selectors").

You do not need to implement this to read ALZ: take bzip2's own decoder (libbzip2, BSD-style licence,
https://sourceware.org/bzip2/) and change only its **framing**, the few fields around each block.

#### Standard bzip2 framing versus ALZ-bzip2 framing

```
standard .bz2 stream:
+-----+---+--------------+----------+---+-----------+--------+ ... +--------------+----------+-----+
| BZh | 9 | 314159265359 | blockCRC | R | origPtr   | block  |     | 177245385090 | combined | pad |
|  24 | 8 |    48 bits   |  32 bits | 1 |  24 bits  |  ...   |     |    48 bits   |  32 bits |     |
+-----+---+--------------+----------+---+-----------+--------+ ... +--------------+----------+-----+
 stream header  block magic                         block data       end magic     stream CRC

ALZ-bzip2 data:
+-----------+-----------+--------+ ... +-----------+-----+
| "DLZ" 01  | origPtr   | block  |     | "DLZ" 02  | pad |
|  32 bits  |  24 bits  |  ...   |     |  32 bits  |     |
+-----------+-----------+--------+ ... +-----------+-----+
 block magic                             end magic
```

The six differences:

| # | Standard bzip2 | ALZ-bzip2 |
|---|---|---|
| 1 | stream header `BZh` + block-size digit `1`..`9` | **none**; the block size is always 9 (900 000) |
| 2 | block magic `0x314159265359` (48 bits) | `44 4C 5A 01` = `DLZ` 01 (32 bits) |
| 3 | 32-bit block CRC after the magic | **none** |
| 4 | 1-bit "randomised" flag | **none**; never randomised |
| 5 | end-of-stream magic `0x177245385090` (48 bits) | `44 4C 5A 02` = `DLZ` 02 (32 bits) |
| 6 | 32-bit combined CRC after the end magic | **none** |

Everything inside a block (origPtr, the 16 + 16 x k bit symbol map, the group count, selectors, code length
tables, the Huffman-coded MTF/RLE2 symbols, then inverse BWT and RLE1) is unchanged.

**The magics are read from the bit stream, 8 bits at a time, MSB first**, exactly where bzip2 would read
its own magic. A block ends wherever its Huffman data ends, so the next `DLZ` 01 usually does **not**
start on a byte boundary. Do not search the bytes for `44 4C 5A 01`; read bits.

Seen side by side for the input `Hello`:

```
standard: 42 5a 68 39 | 31 41 59 26 53 59 | 1a 54 64 92 | 00 00 00 05 00 00 40 02 ...
          "BZh9"       block magic          block CRC     R + origPtr + ...

ALZ:      44 4c 5a 01 | 00 00 00 0a 00 00 80 04 ...
          "DLZ" 01      origPtr + ...

bits after the CRC (standard) and after "DLZ" 01 (ALZ):
standard: 0|0000000 00000000 00000000 00000101 ...     the R bit (0), then origPtr ...
ALZ:        00000000 00000000 00000000 00001010 ...     origPtr straight away
```

Because the 1-bit randomised flag is gone, every bit that follows sits one place earlier: `...0101` becomes
`...1010` (`05` becomes `0A`). The whole `Hello` stream is 26 bytes in ALZ-bzip2 against 43 in standard
bzip2.

There is no CRC inside ALZ-bzip2 at all. **The only check is the entry's CRC-32 from the local header.**

#### Adapting libbzip2's decoder

The changes are all in two files (the reference copy, with every change marked, is `vendor/bzip2/`):

| File | Change |
|---|---|
| `decompress.c` | at the start (state `BZ_X_MAGIC_1`): read nothing, set `blockSize100k = 9`, allocate the work arrays |
| `decompress.c` | block header: read 4 bytes; require `44 4C 5A`; 4th byte `01` = a block, `02` = end, anything else = data error |
| `decompress.c` | after the block magic: do not read the 32-bit CRC; set `blockRandomised = False` without reading a bit |
| `decompress.c` | after the end magic: read no combined CRC; the stream is finished |
| `bzlib.c` | `BZ2_bzDecompress`: remove the comparison of the computed and stored block CRC, and of the combined CRC |

Feed the decoder the (decrypted) packed bytes and ask for exactly `u` bytes of output, as for deflate.

This framing was confirmed byte for byte: for the same 1.4 MB input, ALZip 4.9's method-1 data is identical
to what you get by taking Python's `bz2.compress(data, 9)` and rewriting its framing as above (ALZip).

#### Making ALZ-bzip2 test data without ALZip

Turn a standard bzip2 stream into ALZ-bzip2 (this is how `tests/fixtures/alz/make_fixtures.py` does it):

```
bits = the stream after its 4-byte "BZh9" header, as a bit string
out  = ""
p = 0
loop:
    if bits[p:p+48] == end magic:   out += "DLZ" 02 as bits; stop
    (it must be a block magic)
    next = the first block or end magic after p+48
    out += "DLZ" 01 as bits
    out += bits[p+48+32+1 : next]          (drop the 32-bit CRC and the 1-bit flag)
    p = next
pad out with 0 bits to a whole byte
```

Searching for the 48-bit magics inside data can, in principle, find a false match; decode the result and
compare the CRC to be sure.

---

## 9. Encryption

ALZ uses the **traditional PKWARE ZIP encryption** ("ZipCrypto") unchanged (std; PKWARE APPNOTE). It is weak
by modern standards, but a reader must implement it to open such archives.

### 9.1 The keys

Three 32-bit numbers, updated by every byte of plain text:

```
initial:  key0 = 0x12345678   key1 = 0x23456789   key2 = 0x34567890

update(b):
    key0 = crc_step(key0, b)
    key1 = (key1 + (key0 & 0xFF)) * 134775813 + 1          (mod 2^32)
    key2 = crc_step(key2, key1 >> 24)

crc_step(k, b) = crc_table[(k ^ b) & 0xFF] ^ (k >> 8)      (the table of 1.5, no inversion)

stream_byte():
    t = (key2 | 2) & 0xFFFF
    return ((t * (t ^ 1)) >> 8) & 0xFF

decrypt one byte c:
    p = c ^ stream_byte()
    update(p)                 (with the PLAIN byte)
    return p
```

To start, run `update()` over each byte of the password.

Worked example, password `pass` (bytes `70 61 73 73`):

| After | key0 | key1 | key2 |
|---|---|---|---|
| start | `12345678` | `23456789` | `34567890` |
| `p` | `0EC9BC64` | `21593BA2` | `BC52D562` |
| `a` | `70643D33` | `303FFF2A` | `85D9620D` |
| `s` | `76AC25AD` | `A7E2DB34` | `36869394` |
| `s` | `611D53F6` | `7243F4D3` | `495FC1DE` |

The first stream byte is then `0x3E`.

### 9.2 What the password's bytes are

The keys are fed the password's **bytes**, and ALZip uses the bytes in **its own code page**: a Korean
password is fed as **CP949**, not UTF-8 (ALZip; tested with `비밀` = `BA F1 B9 D0`). ASCII passwords are the
same in both. A reader whose user types in UTF-8 should try the password as typed, and if that fails and it
is not ASCII, again converted to CP949 (and the system's code page). The conversion must be exact: a
character the code page lacks must make the attempt fail, not become `?`.

### 9.3 The encryption header

An encrypted entry has 12 extra bytes between its name and its data. They are encrypted too; decrypt them
first, with fresh keys from the password, then **continue with the same keys** into the data:

```
 name | E E E E E E E E E E E C | data data data ...
        11 random bytes       ^ check byte
        `--- decrypt these 12 first ---' `--- then these, keys running on ---'
```

### 9.4 The password check

After decrypting the 12 bytes, compare the last one:

| Descriptor bit 0x08 | The 12th byte must equal |
|---|---|
| clear (every ALZip archive seen) | the top byte of the entry's CRC-32: `crc >> 24` |
| set (spec only, never seen) | bits 8..15 of the entry's time: `(time >> 8) & 0xFF` |

A mismatch means a wrong password. A wrong password still passes this 1-byte check about 1 time in 256, so
the CRC-32 after decoding is the final word. Good practice: check every encrypted entry's byte (a wrong
password rarely passes several), then decode the smallest encrypted file fully and check its CRC.

Empty files in an encrypted archive are **not encrypted** (descriptor `00`, no encryption header) (ALZip).

### 9.5 Worked example

The archive of 2.1 with the password `pass`:

```
00000000  41 4c 5a 01 0a 00 00 00 42 4c 5a 01 05 00 20 71  |ALZ.....BLZ... q|
00000010  69 9d 57 11 00 00 00 82 89 d1 f7 05 05 61 2e 74  |i.W..........a.t|
00000020  78 74 ee 13 c7 94 c5 0d b6 e9 51 1a 91 16 f4 de  |xt........Q.....|
00000030  65 6a 34 43 4c 5a 01 00 00 00 00 00 00 00 00 43  |ej4CLZ.........C|
00000040  4c 5a 02                                         |LZ.|
```

The descriptor is now `11` (width 1, encrypted); the packed size is still 5. Decrypting from offset `22`:

| # | cipher ^ stream byte = plain | |
|---|---|---|
| 0 | `EE ^ 3E = D0` | random |
| 1..10 | ... | random |
| 11 | `16 ^ E1 = F7` | check byte = `0xF7D18982 >> 24` = `F7`: password accepted |
| 12 | `F4 ^ BC = 48` | `H` |
| 13 | `DE ^ BB = 65` | `e` |
| 14 | `65 ^ 09 = 6C` | `l` |
| 15 | `6A ^ 06 = 6C` | `l` |
| 16 | `34 ^ 5B = 6F` | `o` |

For a compressed entry, the decrypted bytes go into the deflate or bzip2 decoder. Decrypt first, decompress
second, always.

---

## 10. Reading one file: the whole pipeline

```
 archive bytes (across volumes)
        |
        v
 [ read c bytes from data_offset ]   (11.1: never trust c before checking it)
        |
        v
 [ decrypt ]  -- only if descriptor & 1; keys from the password, after the 12-byte header
        |
        v
 [ decompress ] -- method 0: copy | 1: ALZ-bzip2 | 2: raw deflate; output capped at u bytes
        |
        v
 [ CRC-32 of the u bytes == stored CRC ? ]  -- no: damaged file (or wrong password)
        |
        v
     the file
```

Work in chunks (say 64 KB) so that a file crossing volumes, or a huge file, never needs to be copied whole:
read a chunk, decrypt it in place, give it to the decompressor, repeat.

---

## 11. Safety: hostile and damaged input

An archive comes from outside; treat every number in it as a claim to be checked.

### 11.1 Sizes

- Sizes are up to 64 bits. Before allocating `u` bytes, compare `u` with a limit you choose (and with what
  your platform can allocate). Refuse, do not truncate.
- Check `c` against the bytes actually left in the archive before using it: `data_offset + c` can overflow.
  Write `c > total - data_offset`, not `data_offset + c > total`.
- Cap the decompressor's output at `u`. A "zip bomb" declares a small size and expands enormously; with the
  output buffer fixed at `u`, it simply fails.
- For method 0, require `c == u`.

### 11.2 Names

If your program ever writes files to disk, refuse names that could escape the target folder:

| Refuse | Example |
|---|---|
| empty names | |
| names longer than 255 bytes (spec) | |
| absolute names | `\abs.jpg`, `/etc/x` |
| drive letters | `C:\x` (byte 1 is `:`) |
| `..` as a path component | `..\evil.jpg`, `a/../../b` |
| NUL bytes | |

Check on the raw bytes: in CP949 none of `.`, `/`, `\`, `:` can be half of a two-byte character.

### 11.3 Stopping cleanly

Every read must check that the bytes exist before using them; every loop must make progress or stop. The
listing stops at the first inconsistency (6.3). Test this with fuzzing (13.3).

---

## 12. A complete reader in pseudo-code

```
struct Entry { name_bytes, attributes, dos_time, descriptor, method, crc,
               packed_size c, size u, data_offset, encrypted, truncated }

function open(volumes):
    if volumes[0] does not start with "ALZ" 01: error "not an ALZ archive"
    stream = join(volumes)                          # section 7.2, as offsets, no copying
    entries = []; pos = 8; damaged = true
    while stream.has(pos, 4):
        sig = stream.read(pos, 4); pos += 4
        if sig in ("CLZ" 01, "CLZ" 02): damaged = false; break
        if sig != "BLZ" 01: break
        if not stream.has(pos, 9): break
        n, attr, time, desc = le16, byte, le32, byte at pos; pos += 9
        w = desc >> 4
        if w not in (0, 1, 2, 4, 8): break
        method = crc = c = u = 0
        if w:
            if not stream.has(pos, 6 + 2w): break
            method = byte; crc = le32 at pos+2; c = le_n(pos+6, w); u = le_n(pos+6+w, w)
            pos += 6 + 2w
        if not stream.has(pos, n): break
        name = stream.read(pos, n); pos += n
        if desc & 1: pos += 12
        e = Entry(..., data_offset = pos, encrypted = desc & 1)
        if pos > stream.size or c > stream.size - pos:
            e.truncated = true; add(e) if listed(e); break
        if listed(e): entries.add(e)       # listed: not a folder, safe name, n <= 255
        pos += c
    return entries, damaged

function read(e, password, limit):
    if e.truncated: error "cut off"
    if e.method not in (0, 1, 2): error "unsupported"
    if e.u > limit: error "too large"
    if e.method == 0 and e.c != e.u: error "damaged"
    src = chunks of stream from e.data_offset, e.c bytes
    if e.encrypted:
        if no password: error "needs a password"
        keys = init(password)
        header = decrypt(keys, stream.read(e.data_offset - 12, 12))
        expected = (e.time >> 8) & 0xFF if e.descriptor & 8 else e.crc >> 24
        if header[11] != expected: error "wrong password"
        src = decrypt(keys, src)                       # keys keep running
    out = buffer of e.u bytes
    method 0: copy src into out
    method 2: raw-inflate src into out, stop at e.u
    method 1: ALZ-bzip2-decode src into out, stop at e.u
    if fewer than e.u bytes came out: error "damaged"
    if crc32(out) != e.crc: error "damaged (or wrong password)"
    return out
```

The reference implementation of exactly this, in C, is `src/core/alz.c` (about 550 lines), with the API in
`include/rubraview/alz.h`: `rubraview_alz_open_volumes`, `rubraview_alz_set_password`,
`rubraview_alz_read_entry`, `rubraview_alz_volume_path`, `rubraview_alz_volume_number`,
`rubraview_alz_volume_is_last`.

---

## 13. Testing your reader

### 13.1 Test archives in this repository

`tests/fixtures/alz/` (details and inputs in its `README.md`):

| Archive | Made by | Exercises |
|---|---|---|
| `defaults.alz` | ALZip (EggDotNet's test data) | deflate, 6 files, sizes up to 1.2 MB |
| `alzip/a49_max.alz` | ALZip 4.9 | **bzip2** (method 1), a 2-block file |
| `alzip/a49_pass.alz` | ALZip 4.9 | bzip2 + encryption (`pass`) |
| `alzip/a49_vol.*` | ALZip 4.9 | bzip2 in 3 volumes |
| `alzip/a503_pass.alz` | ALZip 5.03 | deflate + encryption |
| `alzip/a67_*` | ALZip 6.7 | deflate, encryption, volumes |
| `alzip/a1237_*` | ALZip 12.37 | stored, deflate, `pass`, Hangul password `비밀` (CP949), 3 volumes |
| `stored`, `deflate`, `bzip2`, `encrypted`, `split*` | made from the spec | every width 1/2/4/8, folders, empty files, time-based check byte |
| `truncated`, `badsig`, `badcrc`, `badnames`, `stray.*` | made from the spec | damage, unsafe names, a stray volume |

The inputs of the `alzip/` archives are listed with their sizes and CRC-32s, so your reader's output can be
checked without any other tool.

### 13.2 Making your own

- With ALZip 6.7 or later on Windows: `ALZipCon.exe -a [-m0..2] [-pPASSWORD] [-vSIZE] SOURCE OUT.alz`
  (8.12, 12.37; the output name decides ALZ), or `ALZip.exe -a ...` in 6.7 (it needs a desktop session).
  These write stored or deflate only.
- bzip2 needs ALZip 4.9 or older; it has no command line (its "new archive" dialog: type alz, rate maximum).
- Without ALZip: write archives from the tables in this guide (as `make_fixtures.py` does), using a standard
  deflate library, a standard bzip2 library plus the reframing of 8.3, and ZipCrypto from section 9.

### 13.3 Fuzzing

Take valid archives, flip random bits (in headers and data), cut them at random lengths, and read every
entry of every result under AddressSanitizer / UndefinedBehaviorSanitizer (or your language's equivalent).
Nothing may crash, hang or read out of bounds; an error is the right answer. The reference reader came through
about 29 000 such archives (100 000 reads; four runs of 45 seconds) without a fault.

---

## 14. What ALZip versions actually write

| Version | ALZ methods it writes | Command line | Notes |
|---|---|---|---|
| 4.9 (4.9.0.66) | bzip2 ("maximum"); stored ("no compression"): the only two ALZ choices | none (GUI only) | `CLZ` 01 record carries 4 non-zero bytes |
| 5.03 (file version 4.99.0.1) | deflate ("normal" tested); offers normal, fast, faster, no compression | none (GUI only) | "maximum" is no longer offered for ALZ; its volume splitting failed on the test machine ("not enough disk space" with 49 GB free) |
| 6.32 | (not tested: no command line) | none | |
| 6.7 | deflate (maximum and default tested) | `ALZip.exe -a -m# -p -v` | |
| 8.12 | deflate (`-m2` tested) | `ALZipCon.exe` | |
| 12.37 | stored (`-m0`), deflate (`-m1`, `-m2`); `-m3`, `-m4` write nothing for ALZ | `ALZipCon.exe -a` | the output name ending in `.alz` makes it write ALZ |

Common to all: `/` between folders; CRC-based check byte (flag 0x08 never set); empty files never encrypted;
size width 2 for files under 65 536 bytes and 4 above (width 1 and 8 were never seen, but are valid); 8 bytes
after `CLZ` 01; volumes no smaller than 64 KB (12.37).

---

## 15. Open points

- **Descriptor flag 0x08** (password check from the time) is in the spec but no ALZip version tested sets it;
  the reference reader implements it as the spec says, tested only on generated archives.
- **The 4 non-zero bytes ALZip 4.9 writes after `CLZ` 01** (and in volume tails) are unexplained (perhaps a
  CRC). Skipping them is safe.
- **The unknown byte after the descriptor and after the method** were 0 in every archive seen.
- **The `0A 00` in the archive header** is constant in every archive seen.
- **Older ALZip versions** (before 4.9) were not available; they may differ.

Questions and answers gathered while building the reader: `docs/cleanroom/questions-alz-decoder.md`.

---

## 16. Glossary

| Term | Meaning |
|---|---|
| ALZ | ESTsoft's archive format, written by ALZip before it switched to EGG |
| BWT | Burrows-Wheeler transform, the heart of bzip2 |
| code page | a table mapping bytes to characters (CP949 = Korean Windows) |
| CRC-32 | 32-bit checksum used to detect damage (1.5) |
| deflate | LZ77 + Huffman compression of RFC 1951 |
| descriptor | the byte in each local header giving size width and encryption flags |
| entry | one file or folder in the archive |
| little-endian | least significant byte first (1.2) |
| LE16 / LE32 | a 2- / 4-byte little-endian number |
| MSB first | a bit stream read from each byte's highest bit (1.4) |
| packed size `c` | bytes of data in the archive (excluding the encryption header) |
| unpacked size `u` | bytes of the file once decoded |
| signature | fixed bytes that mark a record (1.3) |
| volume | one file of a split archive (`.alz`, `.a00`, ...) |
| ZipCrypto | the traditional PKWARE ZIP encryption (9) |
