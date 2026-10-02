# ALZ archive format — a specification for implementers

Written 2026-10-01 for Rubraview (owner: "alz 압축해제 프로그램이 있습니다. 이것도 역시 스펙 작성하고 그걸 보고
구현하는 것으로 진행하지요"). It describes the format in words, tables and pseudo-code so that a decoder can
be written from this document alone. No code was copied into it.

## Sources

- **unalz 0.65** by kippler (Copyright (C) 2004-2009 kippler@gmail.com), zlib licence —
  https://github.com/kippler/unalz, http://kippler.com/win/unalz/. Read: `UnAlz.h`, `UnAlz.cpp`,
  `UnAlzBz2decompress.c`, `UnAlzbzlib.c`, `UnAlzBzip2.cpp`. (Its file still carries, commented out, some
  older GPL code from CZipArchive; that code was not read for this document and is not described here.)
- **bzip2 1.0.5** by Julian Seward, bzip2 licence (BSD-style) — the stock `decompress.c` / `bzlib.c`, read
  only to compare with unalz's modified copies.
- **PKWARE APPNOTE** ("ZIP File Format Specification") — the traditional ZIP encryption, which ALZ uses as is.
- RFC 1951 (DEFLATE).

ESTsoft, who made the format, has published no specification of it; unalz's author worked it out (2004).
Fields unalz never interprets are marked *unknown* here and must be skipped, not trusted.

All integers are little-endian. Offsets below are relative to the start of the structure being described.

## 1. Layout

An ALZ archive is a sequence of records, each opening with a 4-byte signature:

| Signature (bytes) | As LE uint32 | Record |
|---|---|---|
| `41 4C 5A 01` ("ALZ" 01) | 0x015A4C41 | archive header — first in the file |
| `42 4C 5A 01` ("BLZ" 01) | 0x015A4C42 | local file header (one per file or folder), followed by its data |
| `43 4C 5A 01` ("CLZ" 01) | 0x015A4C43 | central directory record |
| `43 4C 5A 02` ("CLZ" 02) | 0x025A4C43 | end of central directory — the last record |
| `43 4C 5A 03` ("CLZ" 03) | 0x035A4C43 | end of a volume tail, in split archives (§2) |

Read records one after another until the end record or the end of the data. Any other signature means the
archive is damaged from there on; what was listed before it can still be read (unalz's behaviour: "files whose
end is cut off are extracted up to the damage").

### 1.1 Archive header (after "ALZ" 01)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | *unknown* (skip) |

### 1.2 Local file header (after "BLZ" 01)

Fixed part, 9 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | name length `n` |
| 2 | 1 | attributes: 0x01 read-only, 0x02 hidden, 0x10 folder, 0x20 file |
| 3 | 4 | modification time, MS-DOS date/time (high 16 bits date, low 16 bits time) |
| 7 | 1 | descriptor (below) |
| 8 | 1 | *unknown* |

Descriptor bits:

| Bits | Meaning |
|---|---|
| 0x01 | the file is encrypted (§4) |
| 0x08 | "data descriptor" flag: changes the password check byte (§4.2) |
| 0xF0 | the width `w` of the two size fields: `w = descriptor >> 4`, one of 0 (no size fields), 1, 2, 4, 8 bytes |

If `w` is not 0, these follow:

| Size | Field |
|---|---|
| 1 | compression method: 0 stored, 1 ALZ-bzip2 (§3.2), 2 raw deflate (§3.1); others unknown |
| 1 | *unknown* |
| 4 | CRC-32 of the uncompressed data (the usual ZIP / zlib CRC-32, initial 0xFFFFFFFF, final XOR) |
| `w` | compressed size `c` (unsigned, little-endian, `w` bytes) |
| `w` | uncompressed size `u` (same) |

Entries with `w = 0` (folders, empty files) have no method, CRC or sizes; treat as 0. ALZip never encrypts an
empty file, even in an encrypted archive: its descriptor is 00, with no encryption header (observed, 2026-10-01).

Then:

| Size | Field |
|---|---|
| `n` | the name — bytes in the code page of the machine that made the archive (in practice CP949, Korean); `/` separates folders in every ALZip version observed (4.9 to 12.37), though `\` should be accepted too. No flag marks UTF-8. Names longer than 255 bytes should be refused or cut. |
| 12 | only if encrypted: the encryption header (§4.1) |
| `c` | the file's data (encrypted if the descriptor says so). `c` does **not** count the 12-byte encryption header. |

The next record's signature follows the data directly.

### 1.3 Central directory record (after "CLZ" 01)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | *unknown* (observed 0) |
| 4 | 4 | *unknown*: 0, except in ALZip 4.9's split archives, where it is non-zero and not the CRC-32 of the volume |

Skip **8** bytes (corrected 2026-10-01: an earlier version of this spec said 12; every archive ALZip 4.9 to 12.37
wrote, and EggDotNet's `defaults.alz`, has 8, then `"CLZ" 02`). It carries no file list: the local headers are the
list. A reader that ends the listing at the first `"CLZ" 01` or `"CLZ" 02` does not depend on this size.

### 1.4 End of central directory (after "CLZ" 02)

No further fields: the archive ends here.

## 2. Volumes (split archives)

A split archive is named `name.alz`, `name.a00`, `name.a01`, ..., `name.a99`, `name.b00`, ... — volume `i`
(counting the `.alz` as 0) for `i ≥ 1` has the extension `x` + two digits where `x = 'a' + (i-1) / 100` and the
digits are `(i-1) % 100`, zero-padded. Volumes are found by name, in that order, until one is missing.

The volumes join into one logical byte stream, the archive described in §1:

- every volume **except the first** starts with an 8-byte volume header — skip it;
- every volume **except the last** ends with a 16-byte volume tail — skip it.

So volume `i` contributes its bytes `[h_i, size_i − t_i)`, with `h_0 = 0`, `h_i = 8` otherwise, and `t_i = 16`
except `t_last = 0`. Records and file data may cross from one volume into the next anywhere.

The pieces, as ALZip writes them (observed 2026-10-01; the source does not describe them):

| Piece | Bytes |
|---|---|
| volume header (8) | `"ALZ" 01`, then `0a 00`, then the volume number as 16 bits little-endian (1 for `.a00`) |
| volume tail (16) | `"CLZ" 01`, 8 bytes (as in §1.3), `"CLZ" 03` |

So a `.alz` that ends in `"CLZ" 02` is whole, and a `.a00` beside it is not one of its volumes; a volume whose
header names another number ends the set. A reader may use this to check volumes found by name, and should
still accept a header that is not `"ALZ" 01` (skipping its 8 bytes).

## 3. Compression

The CRC-32 in the local header is of the uncompressed bytes; a mismatch means a damaged file (or, for an
encrypted one, a wrong password — §4.2 catches most of those earlier).

### 3.1 Method 2: raw deflate

The data is a raw DEFLATE stream (RFC 1951) — no zlib header, no gzip header. Decode until the uncompressed
size is reached or the stream ends.

### 3.2 Method 1: ALZ-bzip2

A bzip2 stream with its framing cut down. Decoding is bzip2's (Burrows–Wheeler, MTF, Huffman, as in bzip2
1.0.x) with these differences from a standard `.bz2` stream:

1. **No stream header.** A standard stream begins with the bytes `B`, `Z`, `h` and a block-size digit
   `'1'..'9'`. ALZ-bzip2 has none of them; the block size is always 9 (900 000 bytes), as if the digit were `'9'`.
2. **Block header.** Where bzip2 has the 48-bit block magic `0x314159265359`, ALZ-bzip2 has the 32 bits
   `0x44 0x4C 0x5A 0x01` ("DLZ" 01). Where bzip2 has the 48-bit end-of-stream magic `0x177245385090`, it has
   `0x44 0x4C 0x5A 0x02` ("DLZ" 02). These are read from the **bit stream**, 8 bits at a time, most significant
   bit first, exactly as bzip2 reads its magic — they are not necessarily byte-aligned in the data.
   After "DLZ" any fourth byte other than 01 or 02 is an error.
3. **No block CRC.** bzip2's 32-bit block CRC that follows the block magic is absent; nothing is checked per block.
4. **No randomised bit.** bzip2's 1-bit "randomised" flag after the block CRC is absent; blocks are never
   randomised.
5. The block then continues as in bzip2: the 24-bit original pointer, the 16 + 16·k bit symbol map, the
   Huffman group count and selectors, the code length tables, and the MTF / Huffman-coded data, then the
   inverse BWT and the final run-length step — unchanged.
6. **No stream trailer.** After "DLZ" 02 there is no combined CRC (bzip2 has 32 bits there): the stream ends.

Pseudo-code of the per-block framing (bit reader `bits(k)`, MSB first):

```
block_size = 9                                   # x 100000
loop:
    m = bits(8), bits(8), bits(8), bits(8)
    if m == "DLZ" 02: end of stream
    if m != "DLZ" 01: error
    orig_ptr = bits(24)
    ... the rest of a bzip2 block, with randomised = false ...
    output the block
```

The only check left is the file's CRC-32 from the local header.

### 3.3 Method 0: stored

The data is the file, `c` (= `u`) bytes.

## 4. Encryption

ALZ uses the **traditional PKWARE ZIP encryption** ("ZipCrypto") as described in PKWARE's APPNOTE,
§"Traditional PKWARE Encryption", without changes to the cipher.

### 4.1 Keys and decryption

```
key0 = 0x12345678, key1 = 0x23456789, key2 = 0x34567890
for each byte p of the password:  update(p)

update(b):
    key0 = crc32_byte(key0, b)                   # one step of the CRC-32 table update, no pre/post inversion
    key1 = (key1 + (key0 & 0xFF)) * 134775813 + 1   (mod 2^32)
    key2 = crc32_byte(key2, key1 >> 24)

stream_byte():  t = (key2 | 2) & 0xFFFF;  return ((t * (t ^ 1)) >> 8) & 0xFF

decrypt(c):  p = c ^ stream_byte();  update(p);  return p
```

`crc32_byte(k, b) = table[(k ^ b) & 0xFF] ^ (k >> 8)` with the standard reflected CRC-32 table
(polynomial 0xEDB88320).

The password's bytes are those typed, in the archive maker's code page: ALZip stores a Korean password as CP949
bytes (confirmed 2026-10-01 with a two-syllable Hangul password, which opens with its CP949 bytes and not with its UTF-8 ones). A reader
whose password box gives Unicode tries the password in UTF-8, then in the chosen code page, CP949 and the system's.

For each encrypted file: initialise the keys from the password, decrypt the 12-byte encryption header
(§1.2), then go on decrypting the `c` data bytes with the same running keys, then decompress.
For ALZ-bzip2 the bytes are decrypted before they reach the bzip2 decoder, exactly as for deflate.

### 4.2 Password check

After decrypting the 12-byte header, its last byte is compared with:

- if the descriptor's 0x08 bit is set: bits 8..15 of the local header's time field
  (`(time >> 8) & 0xFF`);
- otherwise: the top byte of the file's CRC-32 (`crc >> 24`).

A mismatch means the password is wrong. (One byte: a wrong password passes about 1 time in 256; the CRC-32
check after decoding catches the rest.)

## 5. Notes for a reader

- Archives mix encrypted and plain entries; check each.
- The archive header is required to call the file an ALZ archive; the first 4 bytes are "ALZ" 01.
- Sizes are up to 64 bits (`w = 8`).
- Names: decode with the reader's chosen code page (default CP949); `\` and `/` both separate folders;
  refuse `..` components and absolute names.
- No solid mode, no per-archive comment field known.

## Test data known

- EggDotNet (MIT): `src/EggDotNet.Tests/test_files/defaults.alz`.
- Other ALZ files must be made with ALZip 7.x or earlier (later versions write EGG); volumes and the bzip2
  method need such files.
