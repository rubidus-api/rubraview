# EGG archive format (container, without AZO)

Written 2026-10-01 by the `egg-container` clean-room session, **from sample files alone**: archives made with
ALZip 12.37 (`ALZipCon.exe`, its command line) from inputs this session generated, on the project's Windows 11
test VM. No EGG implementation and no EGG specification was read. Standards used to confirm the payloads:
RFC 1951 (deflate), bzip2 1.0.8's documentation, the LZMA SDK, PKWARE's APPNOTE (traditional encryption and
the WinZip AES notes it refers to), FIPS 197 (AES), FIPS 180-4 / RFC 2104 / RFC 8018 (SHA-1, HMAC, PBKDF2),
and KISA's published LEA specification ("128비트 블록암호 LEA 규격서", with its reference values).

Every field below names the sample (in `tests/fixtures/egg/`, made as `tests/fixtures/egg/README.md` says)
and the offset that showed it. Offsets are hexadecimal, numbers are little-endian unless said otherwise.
What no sample showed is marked **unobserved**; a reader should refuse it rather than guess.

## 1. Overall layout

```text
archive header   "EGGA", version, archive id, reserved
archive fields   zero or more extra fields (solid flag, volume link)
END              08E28222
then, repeated:
  file header    0A8590E3, file id, file length
  file fields    name, Windows info, encryption ...
  END
  block          02B50C13 + method, sizes, CRC; END; packed bytes   (none for an empty file or folder)
END              08E28222   -- the archive's last four bytes
```

Every structure starts with a four-byte signature (read as a little-endian u32). Seen in the samples:

| Signature  | Bytes on disk | Meaning                               |
|------------|---------------|---------------------------------------|
| `41474745` | `45 47 47 41` | archive header ("EGGA")               |
| `08E28222` | `22 82 E2 08` | END: closes a field list, the archive |
| `0A8590E3` | `E3 90 85 0A` | file header                           |
| `0A8591AC` | `AC 91 85 0A` | file name field                       |
| `2C86950B` | `0B 95 86 2C` | Windows file information field        |
| `08D1470F` | `0F 47 D1 08` | encryption field                      |
| `02B50C13` | `13 0C B5 02` | block header                          |
| `24E5A060` | `60 A0 E5 24` | solid-archive field                   |
| `24F5A262` | `62 A2 F5 24` | volume (split) field                  |

## 2. Archive header (14 bytes)

| Offset | Size | Field       | Evidence                                                                          |
|--------|------|-------------|-----------------------------------------------------------------------------------|
| 0      | 4    | signature   | `45 47 47 41` at 0 in every sample                                                |
| 4      | 2    | version     | `00 01` = 0x0100 in every sample                                                  |
| 6      | 4    | archive id  | differs per archive: `store.egg` `c6 0b f0 a0`; used to link volumes (section 6) |
| 10     | 4    | reserved    | 0 in every sample                                                                 |

Then the archive's extra fields (section 3) until END. A plain archive has none: `store.egg` has END at 0x0E.

## 3. Extra fields (archive and file level)

Every field that is not a file header or a block has this shape:

| Offset | Size | Field                                         |
|--------|------|-----------------------------------------------|
| 0      | 4    | signature                                     |
| 4      | 1    | flag byte: **0 in every field of every sample** |
| 5      | 2    | size of the data that follows (u16)          |
| 7      | size | data                                          |

Evidence: `store.egg` name field at 0x22: `ac 91 85 0a | 00 | 09 00 | "hello.txt"`. A reader may skip a field
it does not know by its size. A flag other than 0 is **unobserved** (it may announce a wider size field); this
reader refuses such an archive as unsupported.

A field list ends with END (`22 82 E2 08`).

## 4. File header and its fields

### 4.1 File header (16 bytes)

| Offset | Size | Field     | Evidence                                                                            |
|--------|------|-----------|-------------------------------------------------------------------------------------|
| 0      | 4    | `E3 90 85 0A` | `store.egg` 0x12                                                                |
| 4      | 4    | file id   | 0, 1, 2 ... in archive order: `mixed.egg` ids 0-3 at 0x16, 0x95, 0x876, 0xB11      |
| 8      | 8    | file length (unpacked) | `store.egg` 0x1A: 13 (`hello.txt` is 13 bytes); `emptydir.egg` folder: 0 |

ALZip writes the files sorted by name (`mixed.egg` was made from `hello;lorem;rand1k;page01` and lists
`hello, lorem, page01, rand1k`); a reader must not depend on that.

### 4.2 Name field `0A8591AC`

Data = the path, **UTF-8**, folders separated by `/`, no terminator.

- `store.egg` 0x22: size 9, `hello.txt`.
- `hangul.egg` 0x22: size 16, `ed 95 9c ea b8 80 ec 9d b4 eb a6 84 2e 74 78 74` = UTF-8 of `한글이름.txt`.
- `subdir.egg` 0x22: `sub/deep/nested.txt` (made with `alzipcon -a sub`).

### 4.3 Windows information field `2C86950B`

Data, 9 bytes: modification time as a Windows FILETIME (u64, 100 ns since 1601-01-01 UTC), then one
attribute byte.

- `store.egg` 0x32: `00 7b 99 b6 c2 51 dd 01 | 00` → FILETIME 0x01DD51C2B6997B00 (2026), attribute 0x00.
- `emptydir.egg` 0x31: attribute **0x80** for the folder `emptydir` (file length 0, no block). Files have 0x00
  in every sample. So bit 0x80 marks a folder; other attribute bits are **unobserved**.

### 4.4 Encryption field `08D1470F`

Present only on encrypted files, after the Windows information field. Data byte 0 is the encryption method;
the rest depends on it. Encryption covers the packed bytes only; names and sizes are never encrypted.

| Method byte | Cipher            | Data size | Layout after byte 0                                   | Sample           |
|-------------|-------------------|-----------|-------------------------------------------------------|------------------|
| 0           | ZipCrypto         | 17        | 12-byte encryption header, then CRC-32 of the file (4) | `zipcrypto.egg` 0x42 |
| 1           | AES-128, CTR      | 21        | salt (8), password verifier (2), MAC (10)             | `aes128.egg` 0x42 |
| 2           | AES-256, CTR      | 29        | salt (16), password verifier (2), MAC (10)            | `aes256.egg` 0x42 |
| 5           | LEA-128, CTR      | 21        | salt (8), password verifier (2), MAC (10)             | `lea128.egg` 0x42 |
| 6           | LEA-256, CTR      | 29        | salt (16), password verifier (2), MAC (10)            | `lea256.egg` 0x42 |

Older writers: ALZip 8.6 writes this field's size **7 more** than its data (it counts the field header):
`alzip86_aes256.egg` 0x4A `0f 47 d1 08 | 00 | 24 00` declares 36, the data is 29 bytes (method 2, salt,
verifier, MAC) and END follows at 0x6E. Its ZipCrypto and AES-128 fields declare 24 and 28. All other fields of
8.6 count normally. A reader takes a declared 24, 28 or 36 (sizes the current layout never has) as 17, 21, 29
when END follows there; with that, every 8.6 encrypted sample decrypts and matches its CRC.

ALZipCon's `-tp0` .. `-tp4` gave methods 0, 1, 2, 5, 6. Methods 3 and 4 are **unobserved**. An empty file in
an encrypted archive has no encryption field and no block (`zipcrypto_empty.egg`, `empty.txt` at 0x12).
Section 7 gives the algorithms; every encrypted sample decrypts and matches its CRC with them.

## 5. Block

| Offset | Size | Field            | Evidence                                                              |
|--------|------|------------------|-----------------------------------------------------------------------|
| 0      | 4    | `13 0C B5 02`    | `store.egg` 0x46                                                      |
| 4      | 1    | compression method | see 5.1                                                             |
| 5      | 1    | hint             | see 5.1; not needed to decode                                         |
| 6      | 4    | unpacked size    | `store.egg` 0x4C: 13                                                  |
| 10     | 4    | packed size      | `store.egg` 0x50: 13; `deflate.egg` 0x50: 15                          |
| 14     | 4    | CRC-32 of the unpacked bytes (the ZIP / IEEE CRC) | `store.egg` 0x54: `2b 3b d1 f4` = CRC-32 of `hello.txt` |
| 18     | 4    | END              | `store.egg` 0x58                                                      |
| 22     | packed size | packed (and, if encrypted, encrypted) bytes | `store.egg` 0x5C: `Hello, EGG!\r\n`   |

The packed size of an encrypted file counts the cipher text only; the encryption header, salt, verifier
and MAC live in the encryption field (`zipcrypto.egg`: packed 15 for a 15-byte deflate stream; `lea128_store.egg`
second file: packed 13953 = the file's own size, stored).

In a non-solid archive each file with data is followed by exactly one block, even at 6 MiB (a 6 291 456-byte
file made one block). More than one block per file is **unobserved**; this reader refuses it. Sizes are u32,
so files of 4 GiB or more are **unobserved**.

### 5.1 Compression methods

| Method | Name    | Payload                                                                 | Samples (ALZipCon level) |
|--------|---------|-------------------------------------------------------------------------|--------------------------|
| 0      | stored  | the bytes themselves                                                    | `store.egg` (`-m0`)      |
| 1      | deflate | raw deflate (RFC 1951), no zlib header: `deflate.egg` 0x5C `f3 48 cd c9 ...` | `deflate.egg` (`-m1`), `-m2` |
| 2      | bzip2   | a complete bzip2 stream as bzip2 1.0.8 writes it: `bzip2.egg` 0x5C `42 5a 68 34` = `BZh4` | `bzip2.egg` (`-m4`) |
| 4      | LZMA    | 2 bytes `04 41`, u16 property size (5), the 5 LZMA property bytes, the LZMA stream without end marker; the block's unpacked size ends it | `lzma.egg` (`-m3`) |
| 3      | AZO (by elimination, **unobserved**) | not decoded here; see `azo-blackbox` | none |

LZMA detail, from `lzma.egg` 0x5C: `04 41 | 05 00 | 5d 00 00 00 01 | 00 24 19 ...`: properties byte 0x5D
(lc=3, lp=0, pb=2), dictionary 0x01000000 (16 MiB). Decoding with an LZMA_Alone header that declares an
unknown size fails for want of an end marker in every LZMA sample, so the end is the unpacked size.
The meaning of `04 41` is not known; it was the same in every sample.

`-m4` ("최적압축", best) chooses per file: `mixed.egg` has bzip2 for `hello.txt` and `lorem.txt`, stored for
`page01.png`, deflate for `rand1k.bin`. No ALZip 12.37 setting, on the command line (`-m0`..`-m4`; `-m5` to
`-m9` write nothing) or in the GUI's 압축방법 list (the same five levels), produced any method 3 block, so
AZO samples could not be made with this version. ALZip 8.12 (English), which ships an `AZO.dll` coder, wrote
none either at any level, GUI type or file type tried (see `docs/cleanroom/questions-egg-container.md`).

Hint byte seen: stored 0; deflate 2 (`-m1`), 4 (`-m2`), 5 (`-m4`); bzip2 4; LZMA 8. It looks like a level and
is ignored when reading.

## 6. Volumes (split archives)

ALZip names them `NAME.vol1.egg`, `NAME.vol2.egg`, ... Each volume starts with its own archive header (with
its own archive id) and a volume field `24F5A262`, data 8 bytes:

| Offset in data | Size | Field                                  |
|----------------|------|----------------------------------------|
| 0              | 4    | archive id of the previous volume, 0 for the first |
| 4              | 4    | archive id of the next volume, 0 for the last      |

`vol.vol1.egg` .. `vol.vol4.egg` (65 536, 65 536, 65 536, 3 687 bytes):

| Volume | id at 0x06    | field at 0x0E: previous, next |
|--------|---------------|-------------------------------|
| 1      | `ba 1e af da` | `00000000`, `14 c7 58 25`     |
| 2      | `14 c7 58 25` | `ba 1e af da`, `15 85 52 54`  |
| 3      | `15 85 52 54` | `14 c7 58 25`, `b8 34 44 df`  |
| 4      | `b8 34 44 df` | `15 85 52 54`, `00000000`     |

Each volume's header ends with END at 0x1D; the bytes after it (from 0x21) continue the archive stream exactly
where the previous volume stopped, with no framing: a file's packed bytes run across volumes (`big.bin`, 200 000
bytes stored, from `vol.vol1.egg` 0x69 into `vol.vol4.egg`), and so may a header. Reading the volumes' bytes
after their headers one after another gives the stream of a single-volume archive (file headers, blocks, final
END). The final END is the last volume's last four bytes. Encrypted volumes are the same
(`aes256_vol.vol1.egg`, `.vol2.egg`).

## 7. Encryption algorithms

Password = the bytes as typed (the samples use ASCII `pass1234`; ALZip's manual allows only letters, digits and
symbols in a password).

### 7.1 ZipCrypto (method byte 0)

PKWARE APPNOTE "Traditional PKWARE Encryption": keys from the password, the 12-byte header from the encryption
field is decrypted first, then the packed bytes. The header's 12th decrypted byte equals the CRC's high byte
(the field's last byte): `zipcrypto.egg` 0x42 data `00 | dc 15 ... c6 | 2b 3b d1 f4`, decrypted header ends in
0xF4. The packed size does not include the 12-byte header.

### 7.2 AES (method bytes 1 and 2)

The WinZip AES scheme the APPNOTE points to, with its own fields moved into the encryption field:

- key material = PBKDF2-HMAC-SHA1(password, salt, 1000 iterations), 2 × key length + 2 bytes:
  encryption key, then authentication key, then the 2-byte password verifier.
- data = AES in CTR mode, the counter a 16-byte **little-endian** number starting at **1**; the key stream
  block for counter n is AES(key, n).
- MAC = the first 10 bytes of HMAC-SHA1(authentication key, cipher text).

All AES samples (`aes128*.egg`, `aes256*.egg`) check: verifier, MAC, then CRC after decompression.

### 7.3 LEA (method bytes 5 and 6)

Same key derivation, verifier and MAC as 7.2 (PBKDF2-HMAC-SHA1, 1000, salt 8 or 16 bytes, key 16 or 32), with
the LEA block cipher, and a **different counter**: a 16-byte **big-endian** number starting at **0**.

Found in `lea128_store.egg` (stored, so the key stream is cipher text XOR plain text): block 0 of the key stream
is LEA(key, 00..00); blocks 1, 2, 255, 256, 257 equal LEA(key, n) with n big-endian, and do not with n
little-endian. LEA itself is KISA's: the implementation used here reproduces the datasheet's three reference
values (LEA-128/192/256, appendix I). All LEA samples check verifier, MAC and CRC.

## 8. Solid archives

Archive field `24E5A060`, size 0 (`solid_deflate.egg` 0x0E: `60 a0 e5 24 00 00 00`). Then all file headers
come first, each closed by END and none followed by a block, and after the last one a single block holds the
files' bytes one after another, in file-header order:

- `solid_deflate.egg`: four file headers 0x19..0xEA, block at 0xEB, method 1, unpacked 15 582 = 13 + 13 953 +
  592 + 1 024, CRC of the concatenation; final END at 0x16D6.
- `solid_lzma.egg`: the same with method 4.

ALZipCon made solid archives with `-s -m1` and `-s -m3`, but refused `-s -m2` (exit 1, reason not shown), a
password with `-s` (exit 1) and, by its manual, volumes with `-s`. More than one solid block is **unobserved**; this reader refuses it.

## 9. Not covered (unobserved)

- AZO (method 3 by elimination); encryption methods 3 and 4; field flags other than 0; more than one block per
  file; archive comments; sizes of 4 GiB or more; self-extracting (`.exe`) archives.
