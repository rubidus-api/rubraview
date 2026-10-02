<!-- Guide: reading RAR archives, from first principles. Written 2026-10-02 by the rar-decoder clean-room session
from docs/specs/rar-decompression.md, the standards it names (FIPS 197, FIPS 180-4, RFC 2104, RFC 8018, RFC 7693,
the BLAKE2 paper, ISO 3309) and the bytes of the fixtures in tests/fixtures/rar/. -->

# Reading RAR archives: a guide from first principles

This guide teaches how to **read** (list, decrypt and decompress) RAR archives: RAR 4 archives (archive format
1.5, compression versions 2.0 and 2.9) and RAR 5 archives (compression versions 5.0 and 7.0). It is written for
someone who has never written a file parser: Part I explains the general ideas (bytes, bits, checksums,
compression, encryption) before Parts II to V apply them to RAR. Creating archives is not covered.

It sits beside `docs/specs/rar-decompression.md` ("the spec"), which is the reference: terse, complete, with
the source of every fact. This guide is the textbook. Section references such as **(spec 9.4)** point into the
spec. Everything RAR-specific here comes from the spec or from bytes of the test fixtures shown in the text; the
fundamentals come from the public standards named in each section. How the spec and the decoder were made, and
what their authors read, is recorded in `docs/cleanroom/provenance-rar-decoder.md` and Part VI.

```
Part I    Fundamentals .......... bytes, bits, integers, checksums, LZ77, Huffman, range coding,
                                  filters, AES-CBC, SHA, HMAC, PBKDF2, BLAKE2
Part II   The container ......... signatures, RAR 4 blocks, RAR 5 headers, names, volumes, solid
Part III  Decompression ......... the window, RAR 2.0, RAR 2.9 (LZ, filters, PPMd), RAR 5 / 7
Part IV   Encryption and checks . keys, encrypted headers and data, MAC'd checksums
Part V    Writing a reader ...... architecture, pseudo-code, limits, testing with oracles
Part VI   How this knowledge was obtained, and how to do the same
Appendix  Tables at a glance
```

---

# Part I. Fundamentals

## I.1 Bytes, hexadecimal, hex dumps

A file is a sequence of **bytes**; a byte holds a number from 0 to 255. Programmers write bytes in
**hexadecimal** (base 16, digits `0-9a-f`), two digits per byte: `0x52` = 5*16 + 2 = 82. A **hex dump** shows a
file as rows of 16 bytes, with the offset (position) of the first byte of the row on the left and the bytes as
text on the right (`.` where a byte is not printable). This is the start of the fixture `rar5_stored.rar`:

```
offset  bytes                                            as text
000000  52 61 72 21 1a 07 01 00 33 92 b5 e5 0a 01 05 06  Rar!....3.......
000010  00 05 01 01 80 80 00 38 30 06 63 2c 02 03 0b 9d  .......80.c,....
```

On Linux, `od -A x -t x1z -v FILE` prints such a dump; `xxd FILE` and `hexdump -C FILE` do the same where
installed. Reading a format means learning what each of these bytes means.

## I.2 Multi-byte integers and endianness

A number larger than 255 takes several bytes. **Little-endian** order stores the least significant byte first.
All fixed-size integers in RAR headers are little-endian (spec, Conventions):

```
bytes in the file:  0d 00            ->  u16 = 0x000d = 13
bytes in the file:  b4 43 a0 95      ->  u32 = 0x95a043b4
                    |  |  |  +-- most significant
                    +-- least significant

value = b[0] + b[1]*256 + b[2]*256^2 + b[3]*256^3
```

Names used in this guide: `u8`, `u16`, `u32`, `u64` are unsigned integers of 1, 2, 4, 8 bytes, little-endian.

## I.3 Variable-length integers (vint)

RAR 5 stores most numbers as a **vint** (spec 3.1): 7 bits of the value per byte, least significant group first;
the top bit (0x80) of a byte says "another byte follows".

```
byte:   1 b b b b b b b      1 = more bytes follow
        0 b b b b b b b      0 = last byte
          \___________/
           7 value bits

value = (b0 & 0x7f) + (b1 & 0x7f) << 7 + (b2 & 0x7f) << 14 + ...
```

Worked examples from `rar5_stored.rar`:

| Bytes | Groups (low first) | Value |
|---|---|---|
| `0a` | 0x0a | 10 |
| `9d 00` | 0x1d, 0x00 | 29 (the second byte only says "nothing more": a padded vint) |
| `a4 83 02` | 0x24, 0x03, 0x02 | 0x24 + 0x03*128 + 0x02*16384 = 33188 (octal 100644, a Unix file mode) |
| `80 80 00` | 0, 0, 0 | 0 (padded to three bytes) |

A vint is at most 10 bytes (64 bits). Writers may pad with extra `0x80` groups, as above; a reader must accept
that and reject anything longer than 10 bytes.

## I.4 Bits and bit streams, most significant bit first

Compressed data is not made of bytes but of **bit fields** of any width. RAR packs them **most significant bit
first** (spec, Conventions): the first bit of the stream is bit 7 (value 128) of the first byte. "Read n bits"
returns an n-bit number whose first-read bit is its highest bit.

```
bytes:      0xB4            0x2F
bits:   1 0 1 1 0 1 0 0   0 0 1 0 1 1 1 1
        |___| |_______|   |_____|
        read 3 -> 0b101 = 5
              read 5 -> 0b10100 = 20
                          read 4 (continues across the byte) -> 0b0010 = 2
```

A practical **bit reader** keeps a 64-bit accumulator: whenever fewer than the needed bits are in it, it shifts
in the next byte at the bottom of the bits it holds. To "peek" n bits it looks at the top n bits without
consuming them; to "skip" it shifts them out. Two rules matter for robustness (spec 7.1):

- Near the end of the data a decoder may **peek** past the last byte (Huffman decoding peeks 15 bits but may use
  only 2). Feed zeros for those missing bits.
- **Consuming** a bit that is not in the file is an error (truncated data). Keep two counters, bits consumed and
  real bits loaded; consumed > loaded means "ran out of data".

**Byte alignment** means skipping to the next multiple of 8 bits. With a counter of bits consumed since the
start of the packed data, aligning is "round the counter up to a multiple of 8".

## I.5 Checksums: CRC-32

A **checksum** is a short number computed from data so that damage changes it. RAR uses **CRC-32** (ISO 3309,
the same as zlib and PNG) for headers and file contents (spec 5.1). A CRC treats the data as a long binary
polynomial and keeps the remainder of a division by a fixed polynomial; in practice:

```
crc = 0xFFFFFFFF
for each byte b:
    crc = crc XOR b
    repeat 8 times:
        if crc is odd: crc = (crc >> 1) XOR 0xEDB88320
        else:          crc = crc >> 1
result = crc XOR 0xFFFFFFFF
```

Check your implementation: the CRC-32 of the 9 ASCII bytes `123456789` is `0xCBF43926`. For speed, compute the
8 inner steps once for all 256 byte values (a 256-entry table): then `crc = (crc >> 8) XOR table[(crc XOR b) &
0xFF]` per byte. The file `noise.bin` in several fixtures has CRC `1fa12d9d`; that is how the tests know a file
came out right.

## I.6 Compression, part 1: LZ77 and the sliding window

Most data repeats itself. **LZ77** compression replaces a repeat by a reference "copy L bytes from D bytes back"
(a **match**: length L, distance D). The decompressor keeps the last bytes it produced in a **window** (also called
the dictionary) and executes two kinds of instruction:

```
literal 'a'          -> output a
literal 'b'          -> output b
literal 'c'          -> output c
match L=6, D=3       -> copy from 3 back, 6 times, one byte at a time:
                        a b c | a b c a b c
                              ^ copies of what was just written
```

Copying **one byte at a time in increasing order** is essential: when D < L the source overlaps the bytes being
written, which is how a short pattern is repeated (D = 1 repeats the last byte L times).

The window is a ring buffer: position `pos` maps to `window[pos mod size]`; a match reads `window[(pos - D) mod
size]`. Its size bounds the largest distance (RAR's "dictionary size", 64 KiB to 4 GiB and beyond).

```
            window (ring)                    write position
  +---------------------------------------+     |
  | ... old data ... | data within reach  |<----+
  +---------------------------------------+
                      <------ D ------>
```

The compressed stream must say *which* instruction comes next and its numbers, in as few bits as possible; that
is the job of the next two sections.

## I.7 Compression, part 2: Huffman codes, and canonical Huffman codes

A **prefix code** gives each symbol a bit string such that no string is the start of another, so a decoder can
read bits until they spell a whole code. **Huffman codes** give frequent symbols short strings.

Sending the code itself would be expensive, so RAR sends only the **length** of each symbol's code and both sides
build the same **canonical** code from the lengths (spec 7.2):

1. Count how many symbols have each length 1..15 (length 0 = symbol unused).
2. The first code of length l is `first[l]`, with `first[1] = 0` and `first[l+1] = (first[l] + count[l]) << 1`.
3. Symbols of the same length get consecutive codes in increasing symbol order.

Worked example, six symbols with lengths `2 1 3 3 0 0`:

```
count[1] = 1 (symbol 1)       first[1] = 0             symbol 1 -> 0
count[2] = 1 (symbol 0)       first[2] = (0+1)<<1 = 2  symbol 0 -> 10
count[3] = 2 (symbols 2, 3)   first[3] = (2+1)<<1 = 6  symbol 2 -> 110, symbol 3 -> 111

decoding tree:          0 -> symbol 1
                        1 -> 0 -> symbol 0
                             1 -> 0 -> symbol 2
                                  1 -> symbol 3
```

Two error cases (spec 7.2): **over-subscribed** lengths (more codes than fit; e.g. `1 1 1`: three 1-bit codes)
are invalid data; **incomplete** sets (some bit strings mean nothing, e.g. a single symbol of length 1) are allowed,
but reading one of the meaningless strings is a data error.

Fast decoding: peek 15 bits (the longest code). For short codes, look the first 10 bits up in a 1024-entry
table that gives symbol and length; otherwise, for l = 11..15, the code is of length l if the 15 peeked bits,
read as a number, are below the end of length l's range (`(first[l] + count[l]) << (15 - l)`).

**Code lengths of the code lengths.** RAR even compresses the list of lengths: it first sends a small code (20
symbols, its lengths 4 bits each) whose symbols mean "length 0..15" or "repeat the previous length n times" or
"n zeros" (spec 7.3; the RAR 2.0 variant in 8.2). This small code is the **precode**.

## I.8 Compression, part 3: context modelling and range coding (PPMd)

RAR 2.9 can switch to **PPMd** (variant H, by Dmitry Shkarin), a different kind of compressor. Instead of
matches, it predicts each next byte from the bytes before it (its **context**) and keeps statistics for contexts
of length up to an **order** (2..64 here). A **range coder** then turns those probabilities into bits: the coder
holds an interval `[low, low + range)`; each symbol narrows it in proportion to its probability; the decoder
reads the compressed bits as a number `code` and asks which symbol's slice contains it.

```
range before:  |---------------------------------------------|
symbol slices: |   'e' 40%      | 't' 25% |  ...  | escape 5% |
code falls ->               ^ here: the symbol is 'e'; range becomes 'e''s slice
```

When the model has never seen the next byte in the current context it codes an **escape** and tries a shorter
context. This guide does not re-derive the model: RAR uses it exactly as the LZMA SDK's public-domain `Ppmd7.c`
implements it, with the "original" range coder of `Ppmd7aDec.c` (spec 11). Part III.6 explains how RAR wraps it.

## I.9 Filters (preprocessing)

Some data compresses better after a reversible transformation, a **filter**. The compressor applies it before
compressing; the decompressor undoes it after decompressing a stretch of output. Examples in RAR (spec 10.4,
12.5):

- **E8 / E8E9 (x86 code)**: in x86 machine code, `E8` (CALL) and `E9` (JMP) are followed by a 32-bit *relative*
  address. Calls to the same function from different places have different relative addresses but the same
  absolute one. The compressor rewrites them as absolute; the decoder converts them back.
- **Delta**: stores differences between successive samples of a channel (good for tables and images).
- **ARM, Itanium**: the same idea as E8 for those processors' branch instructions.
- **RGB, Audio**: predictive filters for pictures and sound.

A filter covers a range of output positions `[start, start + length)`. The decoder must not hand out those bytes
until the whole range is decoded and transformed. Matches later in the stream copy the **unfiltered** bytes
from the window.

## I.10 Encryption: AES and CBC mode

**AES** (FIPS 197) is a block cipher: with a key (128 or 256 bits here) it turns any 16-byte block into another
16-byte block, and back. To encrypt more than 16 bytes, RAR uses **CBC mode** (cipher block chaining):

```
        C0 = IV (initial vector, 16 bytes)
plaintext:   P1            P2            P3
              |             |             |
  IV ------->XOR    +----->XOR    +----->XOR
              |     |       |     |       |
           [AES-E]  |    [AES-E]  |    [AES-E]
              |     |       |     |       |
ciphertext:  C1 ----+      C2 ----+      C3

decryption:  Pi = AES-decrypt(key, Ci) XOR C(i-1)
```

So decrypting block i needs the ciphertext block before it. When the data is read in pieces (several volumes,
several reads), keep the last ciphertext block of one read for the next: one **chain** (spec 6). The plaintext is
padded to a multiple of 16 bytes; the reader cuts the padding by the known file size.

Test your AES with FIPS 197 appendix C: key `000102...0f`, plaintext `00112233445566778899aabbccddeeff` encrypts
to `69c4e0d86a7b0430d8cdb78070b4c55a` (AES-128); with key `000102...1f`, to
`8ea2b7ca516745bfeafc49904b496089` (AES-256).

## I.11 Hashes, HMAC, key derivation

- A **hash** (SHA-1, SHA-256: FIPS 180-4) maps any data to a fixed-size digest (20 or 32 bytes) such that it is
  infeasible to find two inputs with the same digest. Check: SHA-256 of `abc` is `ba7816bf...f20015ad`.
- **HMAC** (RFC 2104) is a hash keyed with a secret: `HMAC(K, m) = H((K xor opad) || H((K xor ipad) || m))`.
  Check: RFC 4231 test case 1.
- **PBKDF2** (RFC 8018) turns a password into a key by iterating HMAC many times, so guessing passwords is slow:
  `U1 = HMAC(P, salt || 00000001)`, `Uj = HMAC(P, U(j-1))`, key = `U1 xor U2 xor ... xor Uc`.
- A **salt** is random data stored in the archive and mixed into the derivation, so the same password gives
  different keys in different archives.

## I.12 BLAKE2s and BLAKE2sp

**BLAKE2s** (RFC 7693) is a fast 32-byte hash. **BLAKE2sp** (the BLAKE2 paper) runs 8 BLAKE2s instances in
parallel and hashes their results (spec 5.3):

```
data, in 64-byte chunks:   c0 c1 c2 c3 c4 c5 c6 c7 c8 c9 ...
                            |  |  |  |  |  |  |  |  |  |
leaf 0 gets c0, c8, c16 ... |  |  |  |  |  |  |  |  |  +-- leaf 1
leaf 1 gets c1, c9, ...     v  v  v  v  v  v  v  v  v
                          leaf0 ... leaf7  (each a BLAKE2s with its own parameter block)
                             \    |    /
                       root: BLAKE2s(D0 || D1 || ... || D7)  -> 32-byte BLAKE2sp
```

The parameter blocks differ per node (node offset 0..7, node depth 0 for leaves and 1 for the root, fanout 8,
depth 2, inner length 32), and the last leaf and the root are "last nodes", which flips one more word in the
final compression (spec 5.3's table). Checks: BLAKE2sp of the empty string is `dd0e8917...00f2ca4f`, of `00 01
02` is `ed14413b...2f423c46`.

---

# Part II. The container

## II.1 Two formats, one name

| Name in this guide | Signature (first bytes) | Written by | Compression inside |
|---|---|---|---|
| RAR 4 (archive format 1.5) | `52 61 72 21 1A 07 00` ("Rar!" 1A 07 00) | RAR 1.5 to 4.x, and later versions with `-ma4` | versions 1.5 (not covered), 2.0, 2.9 |
| RAR 5 | `52 61 72 21 1A 07 01 00` | RAR 5.0 and later | versions 5.0 and 7.0 |

The signature is normally at offset 0. A **self-extracting archive** (SFX) is a program with the archive appended;
the archive then starts later. Search every offset of the first 1 MiB for either signature and ignore what comes
before it (spec 1). The fixtures `rar3_sfx_stub.sfx` and `rar5_sfx_stub.sfx` have their signatures at offsets
127208 and 244864 (not multiples of 16, so a reader that only looks at aligned offsets misses them).

The overall picture of both formats:

```
RAR 4                                         RAR 5
+------------------------+                    +------------------------+
| signature (7)          |                    | signature (8)          |
+------------------------+                    +------------------------+
| block: main header     |                    | [encryption header]    |  only with encrypted headers
+------------------------+                    +------------------------+
| block: file header     |                    | main header            |
|   data area (packed)   |                    +------------------------+
+------------------------+                    | file header            |
| block: file header     |                    |   data area (packed)   |
|   data area            |                    +------------------------+
+------------------------+                    | service header (CMT,..)|
| ... other blocks ...   |                    |   data area            |
+------------------------+                    +------------------------+
| block: end of archive  |  (optional)        | end of archive header  |
+------------------------+                    +------------------------+
```

## II.2 RAR 4 blocks

After the signature, a RAR 4 archive is a chain of **blocks**, each starting with a 7-byte block header (spec
2.1):

```
offset: 0      2      3          5          7
        +------+------+----------+----------+------------------ ... ---+---------------------+
        | CRC  | TYPE | FLAGS    | SIZE     | type-specific fields      | data area           |
        | u16  | u8   | u16      | u16      |                           | (ADD_SIZE or        |
        +------+------+----------+----------+------------------ ... ---+  PACK_SIZE bytes)   |
        <---------------------- SIZE bytes (the header) ------------->  +---------------------+

CRC  = low 16 bits of CRC-32 over header bytes 2 .. SIZE-1
FLAGS & 0x8000: a u32 ADD_SIZE follows at offset 7, and that many data bytes follow the header
```

Block types: `0x73` main header, `0x74` file, `0x7A` service ("new sub-block": comments, ACLs, streams, recovery
record), `0x7B` end of archive; `0x75` to `0x79` are old comment, authenticity, sub-block, recovery and signature
blocks. Skip any block you do not need: SIZE bytes, plus ADD_SIZE when flag 0x8000 is set (spec 2.1). Skip unknown
types the same way.

### Worked example: `rar.rar`

```
000000  52 61 72 21 1a 07 00                      signature (RAR 4)
000007  cf 90                                     CRC  0x90cf
000009  73                                        TYPE main header
00000a  00 00                                     FLAGS 0 (not a volume, not solid, ...)
00000c  0d 00                                     SIZE 13
00000e  00 00 00 00 00 00                         reserved (u16 + u32)
000014  84 52                                     CRC  0x5284
000016  74                                        TYPE file header
000017  20 90                                     FLAGS 0x9020 = 0x8000 (data follows) | 0x1000 (EXT_TIME)
                                                               | 0x0020 (dictionary field 1 = 128 KiB)
000019  32 00                                     SIZE 50
00001b  14 00 00 00                               PACK_SIZE 20
00001f  14 00 00 00                               UNP_SIZE 20
000023  03                                        HOST_OS 3 = Unix
000024  42 a2 c8 be                               FILE_CRC 0xbec8a242
000028  b7 76 da 3e                               FTIME (MS-DOS date and time)
00002c  14                                        UNP_VER 20
00002d  30                                        METHOD 0x30 = stored
00002e  08 00                                     NAME_SIZE 8
000030  a4 81 00 00                               ATTR 0x81a4 = Unix mode 100644 (regular file)
000034  74 65 73 74 2e 74 78 74                   FILE_NAME "test.txt"
00003c  80 08 b7 76 da 3e b7 76 da 3e             EXT_TIME: flags 0x0880 (ctime, atime), two u32 times
000046  74 65 73 74 20 74 65 78 74 20 64 6f ...   data area: 20 bytes, "test text document\r\n"
00005a  9d 2f 74 20 90 32 00 ...                  next block (another file header)
```

`FILE_CRC` matches the manifest line `rar.rar|test.txt|20|bec8a242`.

### The main header (0x73), spec 2.2

| Flag | Meaning |
|---|---|
| 0x0001 | this archive is one volume of a set |
| 0x0008 | solid archive (II.7) |
| 0x0010 | new volume naming (`name.partNN.rar`) |
| 0x0080 | block headers are encrypted (IV.2) |
| 0x0100 | first volume |
| 0x0002 | old-style comment embedded (rare; spec 2.2) |

### The file header (0x74), spec 2.3

| Offset | Size | Field |
|---|---|---|
| 7 | u32 | PACK_SIZE: packed size (low 32 bits) |
| 11 | u32 | UNP_SIZE: unpacked size (low 32 bits); all ones = unknown |
| 15 | u8 | HOST_OS: 0 DOS, 1 OS/2, 2 Windows, 3 Unix, 4 Mac, 5 BeOS |
| 16 | u32 | FILE_CRC |
| 20 | u32 | FTIME |
| 24 | u8 | UNP_VER: which algorithm (20/26 = RAR 2.0, 29 = RAR 2.9, 15 = RAR 1.5) |
| 25 | u8 | METHOD: 0x30 stored, 0x31..0x35 compressed |
| 26 | u16 | NAME_SIZE |
| 28 | u32 | ATTR |
| 32 | u32, u32 | HIGH_PACK_SIZE, HIGH_UNP_SIZE: only with flag 0x0100 |
| ... | NAME_SIZE | FILE_NAME |
| ... | 8 | SALT: only with flag 0x0400 |
| ... | var | EXT_TIME: only with flag 0x1000 |

| File flag | Meaning |
|---|---|
| 0x0001 / 0x0002 | split: continues from the previous volume / continues in the next (II.6) |
| 0x0004 | encrypted (with SALT: AES, IV.1; without: older ciphers, not covered) |
| 0x0010 | solid: continues the previous file's decoder state |
| 0x00E0 | 3-bit dictionary field n: 64 KiB << n; n = 7 means **directory** |
| 0x0100 | 64-bit sizes present |
| 0x0200 | Unicode name (II.4) |
| 0x0400 | salt present |
| 0x0800 | name ends in `;version` |

The 64-bit case is real: `rar4_64bit_size.rar` stores 4,499,963,925 = 204,996,629 + 1 * 2^32 bytes.

Times (spec 2.5): MS-DOS times pack seconds/2, minutes, hours, day, month and year-1980 into 32 bits (bit fields
0-4, 5-10, 11-15, 16-20, 21-24, 25-31). EXT_TIME refines them; a viewer can ignore it, but must skip it correctly,
which is easy because it lies at the end of the header and the header's SIZE says where the header ends.

A Unix symbolic link is a file with `HOST_OS` 3..5 and `ATTR & 0xF000 == 0xA000`; its data is the link target.

### End of archive (0x7B), spec 2.6

Flag 0x0001 means "the set continues in the next volume". The end block is optional: `rar_noeof.rar` simply stops
after its last file. Treat "fewer than 7 bytes left" as the end.

## II.3 RAR 5 headers

Every RAR 5 header has the same frame (spec 3.2):

```
+--------+------------+--------+--------+-------------+------------+--------------------+--------------+
| CRC32  | HeaderSize | Type   | Flags  | [ExtraSize] | [DataSize] | type-specific      | extra area   |
| u32    | vint       | vint   | vint   | vint if 1   | vint if 2  | fields             | (records)    |
+--------+------------+--------+--------+-------------+------------+--------------------+--------------+
         <----------------------------- CRC covers these bytes ------------------------------------>
                      <--------------------------- HeaderSize bytes --------------------------->
followed by DataSize bytes of data area (not covered by the CRC)
```

Header types: 1 main, 2 file, 3 service, 4 archive encryption, 5 end of archive. Common flags: 0x0001 extra area
present, 0x0002 data area present, 0x0008 / 0x0010 data continues from the previous / in the next volume. The
**extra area** is the last ExtraSize bytes of the header: a list of records, each `Size (vint) | Type (vint) |
data`, where Size counts from Type to the end of the record; skip records of unknown type.

### Worked example: `rar5_stored.rar`

```
000000  52 61 72 21 1a 07 01 00            signature (RAR 5)
                                           --- main header ---
000008  33 92 b5 e5                        CRC32
00000c  0a                                 HeaderSize 10
00000d  01                                 Type 1 = main
00000e  05                                 Flags 0x05 = extra area | skip-if-unknown
00000f  06                                 ExtraSize 6
000010  00                                 archive flags 0 (not a volume, not solid)
000011  05 01 01 80 80 00                  extra record: size 5, type 1 (locator), flags 1,
                                           quick-open offset = vint 80 80 00 = 0 (padded)
                                           --- file header ---
000017  38 30 06 63                        CRC32
00001b  2c                                 HeaderSize 44
00001c  02                                 Type 2 = file
00001d  03                                 Flags 0x03 = extra area | data area
00001e  0b                                 ExtraSize 11
00001f  9d 00                              DataSize 29 (padded vint)
000021  04                                 file flags 0x04 = CRC32 present
000022  9d 00                              unpacked size 29
000024  a4 83 02                           attributes 33188 = Unix mode 100644
000027  b4 43 a0 95                        data CRC32 0x95a043b4
00002b  80 00                              compression information 0: version 0, stored
00002d  01                                 host OS 1 = Unix
00002e  0e                                 name length 14
00002f  68 65 6c 6c 6f 77 6f 72 6c 64 2e 74 78 74     "helloworld.txt"
00003d  0a 03 13 7e 0e ab 5b 56 e9 0e 1a   extra record: size 10, type 3 (time), flags 0x13 =
                                           Unix time | mtime | nanoseconds; u32 mtime, u32 ns
000048  68 65 6c 6c 6f 20 ... 21 0a        data area: 29 bytes "hello libarchive test suite!\n"
                                           --- end of archive ---
000065  1d 77 56 51 03 05 04 00            CRC32, HeaderSize 3, Type 5, Flags 0x04, end flags 0
```

The CRC32 `0x95a043b4` is the manifest's `rar5_stored.rar|helloworld.txt|29|95a043b4`.

### The file header (type 2), spec 3.5

| Field | Type | Notes |
|---|---|---|
| file flags | vint | 0x0001 directory, 0x0002 mtime present, 0x0004 CRC32 present, 0x0008 size unknown |
| unpacked size | vint | |
| attributes | vint | Windows attributes (host 0) or a Unix mode (host 1) |
| mtime | u32 | if 0x0002 |
| data CRC32 | u32 | if 0x0004 |
| compression information | vint | below |
| host OS | vint | 0 Windows, 1 Unix |
| name length, name | vint, bytes | UTF-8, `/` separates folders |

**Compression information** (spec 3.5):

```
bit:  20     19..15     14..10      9..7      6       5..0
     +----+----------+----------+---------+-------+----------+
     | v0 | fraction | dict N   | method  | solid | version  |
     |data| F (v1)   |          | 0..5    |       | 0 or 1   |
     +----+----------+----------+---------+-------+----------+
dictionary = 128 KiB << N   (+ (that / 32) * F for version 1)
method 0 = stored; 1..5 = compressed (one decoder for all)
version 0 = RAR 5.0 algorithm; version 1 = RAR 7.0 algorithm, unless bit 20 says "really version 0"
```

Example from `rar5_compressed.rar`: compression information `80 05` = 0x280: method 5, N = 0 (128 KiB), version
0. `rar7_v70_md4352m.rar` has 0x13E81: version 1, method 5, N = 15, F = 2: 4 GiB + 2 * 128 MiB = 4352 MiB.

### File extra records (spec 3.6)

| Type | Name | Contents |
|---|---|---|
| 1 | encryption | version, flags (1 = check value present, 2 = checksums are MAC'd), KDF count, salt (16), IV (16), check value (12) |
| 2 | hash | hash type 0 = BLAKE2sp, then 32 bytes |
| 3 | time | high-precision times (see example above) |
| 4 | version | file version number |
| 5 | redirection | links: type, flags, target name |
| 6 | Unix owner | user / group names and ids |
| 7 | service data | |

## II.4 File names

- RAR 5: UTF-8 bytes with `/` between folders (spec 3.5).
- RAR 4 without flag 0x0200: bytes in the archiver's code page, which the archive does not record
  (`rar_unicode.rar` uses Shift-JIS). A viewer guesses or asks, as for ZIP files.
- RAR 4 with 0x0200 and no zero byte in the name: UTF-8.
- RAR 4 with 0x0200 and a zero byte: `narrow name` 00 `encoded UTF-16 name`. The encoding (spec 2.4) is a small
  compression scheme: a "high byte" first, then groups of 2-bit modes:

```
enc = [high] [flags] [data ...] [flags] [data ...] ...
mode 0: one byte b              -> character 0x00bb
mode 1: one byte b              -> character (high << 8) | b
mode 2: two bytes lo, hi        -> character hi:lo
mode 3: one byte L (and corr)   -> copy (L & 0x7F) + 2 characters from the narrow name at the same position,
                                   adding corr to each and using `high` as their high byte if L & 0x80
```

Stop when the encoded bytes run out or the output is as long as the narrow name. In every case replace `\` by
`/`. In `rar4_enc_data.rar` the name `sub\` + a Korean file name decodes with `sub\` copied from the narrow name by
mode 3, then U+D55C by mode 1, U+AE00 by mode 2 (spec 2.4).

## II.5 Directories, links, service headers

Directories (RAR 4 dictionary field 7; RAR 5 file flag 0x0001) and links (RAR 4 Unix mode 0xA000; RAR 5
redirection record) are not pages; a viewer lists regular files only. Service headers (RAR 4 type 0x7A, RAR 5
type 3: `CMT` comment, `QO` quick-open copy of the headers, `ACL`, `STM`, `RR` recovery record) are skipped with
their data. Do not use the quick-open copy: it duplicates the headers, and reading two copies invites
inconsistencies (spec 3.5).

## II.6 Volumes

A big archive can be cut into **volumes** (spec 4):

```
name.part1.rar             name.part2.rar             name.part3.rar
+---------------------+    +---------------------+    +---------------------+
| sig, main (vol. 0)  |    | sig, main (vol. 1)  |    | sig, main (vol. 2)  |
| file A              |    | file B, part 2      |    | file C, part 2      |
| file B, part 1      |    |   (split before)    |    |   (split before)    |
|   (split after)     |    | file C, part 1      |    | end (last)          |
| end (more follows)  |    |   (split after)     |    +---------------------+
+---------------------+    | end (more follows)  |
                           +---------------------+
file B's packed stream = part 1 data || part 2 data   (one compressed stream, one CBC chain)
```

- **Names.** New style: `name.part1.rar`, `name.part2.rar` (digits zero-padded to the same width, e.g. `part01`):
  increment the last number before the extension. Old style (RAR 4 without main flag 0x0010): `name.rar`,
  `name.r00`, `name.r01`, ... `name.r99`, `name.s00` (spec 4.1). RAR 5 main headers also number the volumes (0 for
  the first); check that volume k says k.
- **Split files.** Each part has its own complete file header with the same name and the split flags. The data
  areas of all parts, concatenated in order, form one packed stream: one compressed stream and, if encrypted,
  one AES-CBC chain (spec 4.2). Example: `rar4_vol_enc.part01.rar` and `part02.rar` hold 10108 + 1956 bytes of one
  12064-byte encrypted stream; neither piece is a multiple of 16 on its own.
- Take the algorithm, sizes and encryption parameters from the first part, the final checksum from the last.
  Every non-last part's checksum covers only that part's packed bytes (spec 4.3).
- A part that should continue a file but has a different name, or a volume that is missing, makes that file
  unreadable; the files before it remain readable.

## II.7 Solid archives

In a **solid** archive (RAR 4 main flag 0x0008, RAR 5 main flag 0x0004), all files are compressed as one
continuous stream: file 2 may copy matches from file 1's bytes. Each file is still a separate data area with its
own header (spec 7.5):

```
packed:    [ file 1 data ][ file 2 data ][ file 3 data ]
                  |              |              |
decoder:   window, tables, distances, PPMd model ... carried from one file to the next
output:    [ file 1     ][ file 2      ][ file 3      ]
```

Consequences for a reader:

- To read file k, decode files 0..k-1 first (and throw their output away). Remember where the decoder stopped:
  reading k+1 next only needs file k+1.
- The bit reader restarts at the first byte of each file's data; no bits carry over.
- Decode each file's stream **to its end**, past its unpacked size: the end-of-file code of RAR 2.9 says whether
  the next file begins with new tables, and RAR 5's last block may carry tables or filters the next file needs.
- A file flagged solid continues the previous one; a file not flagged solid starts afresh.

---

# Part III. Decompression

## III.1 Which decoder

| Archive | Field | Value | Decoder |
|---|---|---|---|
| RAR 4 | METHOD | 0x30 | stored: the data area is the file |
| RAR 4 | UNP_VER | 20, 26 | RAR 2.0 (III.4) |
| RAR 4 | UNP_VER | 29 | RAR 2.9 (III.5 to III.7) |
| RAR 4 | UNP_VER | 15, other | RAR 1.5 / unknown: report unsupported |
| RAR 5 | method | 0 | stored |
| RAR 5 | version | 0 (or 1 with bit 20) | RAR 5.0 (III.8) |
| RAR 5 | version | 1 | RAR 7.0 (III.9) |

## III.2 The shape every decoder has

```
          packed bytes (after decryption)
                     |
                     v
              +-------------+
              | bit reader  |   MSB first, counts bits (I.4)
              +-------------+
                     |
                     v
         +------------------------+       tables rebuilt at block starts
         | block / symbol decoder |<----- (precode -> code lengths -> Huffman)
         +------------------------+
             | literals, matches, filter records
             v
   +--------------------------------------+
   | window (ring buffer, >= dictionary)  |
   +--------------------------------------+
             | bytes ready for output
             v
   +--------------------------------------+
   | filter stage: hold back ranges a     |
   | pending filter covers, transform     |
   | them when complete                   |
   +--------------------------------------+
             |
             v
   output, cut at the file's unpacked size; CRC-32 / BLAKE2sp computed on it
```

**Output and the window** (spec 7.4, 7.5). Keep three positions, counted in bytes since the stream began:
`pos` (next byte to decode), `flushed` (everything before it has been output), and the current file's
`[start, end)`. After each symbol, if the undelivered bytes `pos - flushed` approach the window size, deliver
what may go: everything up to the first pending filter's start (or the file's end). A pending filter whose range
is completely decoded is applied to a **copy** of its bytes, the copy is delivered, and `flushed` moves past it.
The window keeps the unfiltered bytes for later matches. A pending filter that cannot complete before the window
fills is broken data.

**Window size.** At least the dictionary size; any larger size works, including sizes that are not powers of two
(then wrap with a comparison instead of a mask). Leave room beyond it for the longest filter range (4 MiB in
RAR 5) and one symbol's output, or a legal filter cannot be held back. Memory is the limit: RAR 7's
`-md6g` fixture needs a 4352 MiB window.

**Distances before the start.** A distance larger than the bytes produced so far reads zeros (spec 7.4); valid
archives do not do it, damaged ones do, and a reader must not read outside its buffer.

## III.3 Tables from code lengths (RAR 2.9 and RAR 5), spec 7.3

```
1. 20 precode lengths, 4 bits each; the value 15 is an escape:
       15 then 0      -> a real length 15
       15 then z > 0  -> z + 2 zero lengths
2. build the precode (canonical Huffman, I.7)
3. read N lengths with it:
       0..15  -> RAR 5: that length.  RAR 2.9: (previous length of this slot + it) & 15
       16     -> repeat the previous length 3 + (3 bits) times
       17     -> repeat the previous length 11 + (7 bits) times
       18     -> 3 + (3 bits) zeros
       19     -> 11 + (7 bits) zeros
4. cut the N lengths into the algorithm's tables and build each
```

"Previous length of this slot" means RAR 2.9 remembers the last lengths and sends differences; a block header
bit (`keep`) says whether to clear them first. RAR 2.0 has its own variant (III.4).

## III.4 RAR 2.0 (spec 8)

Blocks begin with two bits: `audio` and `keep` (0 = clear the remembered lengths). An LZ block then reads 374
lengths (main 298, distance 48, length 28); an audio block reads `channels` (2 bits + 1) times 257 lengths, one
table per channel. The RAR 2.0 precode is 19 lengths of 4 bits (no escape) and its symbols mean: 0..15 add to the
saved length; 16 repeat the previous 3 + (2 bits) times; 17 3 + (3 bits) zeros; 18 11 + (7 bits) zeros.

LZ symbols (spec 8.3), with distances `D[0..3]` (D[0] newest) and last length `L`:

| Symbol | Meaning |
|---|---|
| 0-255 | literal byte |
| 256 | repeat the last match: push a copy of D[0], copy L bytes from it |
| 257-260 | reuse D[s-257]: push it, read a length (length table), add 1 / 2 / 3 when the distance is >= 0x101 / 0x2000 / 0x40000 |
| 261-268 | short distance from SDBASE/SDBITS, length 2 |
| 269 | end of block: a new block header follows |
| 270-297 | length from LBASE/LBITS (+3), distance from the distance table and DBASE/DBITS (+1); +1 / +2 for distances >= 0x2000 / 0x40000 |

"Push d" means `D = [d, D0, D1, D2]`. Note that RAR 2.0 pushes even a reused distance (RAR 2.9 moves it to the
front instead); the fixtures confirm this.

Audio blocks decode, per channel in turn, a delta symbol (256 ends the block) that a small adaptive linear
predictor turns into an output byte. The predictor (spec 8.4) keeps per channel five weights `K`, the last four
deltas, the last byte, and eleven error sums; every 32 bytes it nudges the weight whose error sum was smallest.
Copy spec 8.4 exactly, using signed integers, and multiply by 8 instead of shifting negative numbers left.

RAR 2.0 has **no end-of-file code**: stop when the unpacked size is reached. In a solid archive the next file
continues in the same block (no new header), with the same tables and predictor state. Decoding may run a few
bytes past the size in a final match; those bytes belong to the next file.

## III.5 RAR 2.9 LZ (spec 9)

Each block starts at a byte boundary with one bit: 1 = PPMd block (III.7), 0 = LZ block, then `keep` and the 404
code lengths: main 299, distance 60, low distance 17, repeat length 28.

| Symbol | Meaning |
|---|---|
| 0-255 | literal |
| 256 | end: next bit 1 = end of block (new header follows); 0 = end of file, then one bit: 1 = the next solid file starts with a block header, 0 = it starts directly with symbols using these tables |
| 257 | filter record (III.6) |
| 258 | repeat the last match (L bytes from D[0]) |
| 259-262 | reuse D[s-259], **moved to the front**; length from the repeat-length table (+2) |
| 263-270 | short distance (SDBASE/SDBITS, +1), length 2, pushed |
| 271-298 | length from LBASE/LBITS (+3), long distance below; +1 / +2 for distances >= 0x2000 / 0x40000; pushed |

Long distance: slot `k` from the distance table, `d = DBASE[k] + 1`, `b = DBITS[k]` extra bits. When `b >= 4`
the low 4 bits come from the low-distance table instead of the bit stream (symbol 16 there means "the same low
bits again for the next 16 distances"):

```
if b >= 4:
    d += (read b-4 bits) << 4
    if lowRepeat > 0:  lowRepeat -= 1; d += lowDist
    else: x = next low-distance symbol
          if x == 16:  lowRepeat = 15; d += lowDist
          else:        d += x; lowDist = x
elif b > 0: d += read b bits
```

The tables LBASE, LBITS, DBASE, DBITS, SDBASE, SDBITS are listed in the Appendix (spec 9.3).

## III.6 RAR 3 filters (spec 10)

A filter record (LZ symbol 257, or PPMd escape code 3) is a short byte string: a flags byte, a length, then data
read with its own bit reader. Its numbers use the RAR 3 variable-length form:

```
vmnum: 2 bits t
  t=0: 4 bits      t=1: 8 bits v (if v < 16: 0xFFFFFF00 | v << 4 | 4 more bits)
  t=2: 16 bits     t=3: 32 bits
```

The record says which **program** to run (flags 0x80: a new program number), where (`start` = a number + the
bytes already decoded in this file, +258 with flag 0x40), how long (flag 0x20, else the program's last length),
seven register values (flag 0x10) and, the first time a program is used, its byte code. RAR 3 was designed to
run that byte code in a small virtual machine, but RARLAB's archivers only ever write six standard programs, which
readers recognise by the CRC-32 and length of the byte code and replace with native code (spec 10.3):

| Filter | CRC-32 of the code | Code length | Parameters |
|---|---|---|---|
| E8 | 0xAD576887 | 53 | |
| E8E9 | 0x3CD7E57E | 57 | |
| Itanium | 0x3769893F | 120 | |
| Delta | 0x0E06077D | 29 | R[0] channels |
| RGB | 0x1C2C5DC8 | 149 | R[0] row width, R[1] red byte position |
| Audio | 0xBC85E701 | 216 | R[0] channels |

Any other program needs the virtual machine (spec Appendix A); a reader may report it as unsupported. Filters run
in the order defined; several with the same start and length run one after another on the same bytes (spec
10.2). The algorithms (spec 10.4), in brief:

- **E8/E8E9**: for every `E8` (and `E9`) byte with at least 4 bytes after it, read the 32-bit address `a` at
  file position `p` = offset of the address; if `a` is negative and `a + p` is not, store `a + 2^24`; if `0 <= a <
  2^24`, store `a - p`; then skip the 4 address bytes.
- **Delta**: the block stores channel 0's deltas, then channel 1's, ...; each output byte is the previous output
  of its channel minus the stored byte.
- **RGB**: per colour, predict from the left, upper and upper-left pixels (the one of three closest to `left + up
  - upleft`), then add green back to red and blue.
- **Audio**: per channel, the predictor of RAR 2.0 audio reduced to three weights.
- **Itanium**: in 16-byte instruction bundles of certain templates, convert 20-bit branch targets.

## III.7 PPMd blocks (spec 11)

A PPMd block header, after the block's 1 bit, has 7 bits of flags: 0x20 = new model (a memory size byte follows:
(mem + 1) MiB), 0x40 = new escape byte follows, low 5 bits = order (`order = (f & 0x1F) + 1`, and orders above 16
map to `16 + (order - 16) * 3`, reaching 64). Then the range decoder takes 4 bytes. The model is the LZMA SDK's
`Ppmd7` with the `Ppmd7a` range coder; feed it the next bytes of the packed stream, and only the bytes it asks
for, because the next block header starts right after.

Decoded symbols are bytes; one byte value, the **escape byte** (2 at the start), introduces a command:

| After the escape byte | Meaning |
|---|---|
| 0 | end of block: a new block header follows (may switch to LZ) |
| 1, 6..255 | the escape byte itself as a literal |
| 2 | end of file |
| 3 | filter record, its bytes decoded as PPMd symbols |
| 4 | 3 bytes distance (big-endian) + 1 byte length: copy length + 32 bytes from distance + 2 |
| 5 | 1 byte length: copy length + 4 bytes from distance 1 |

## III.8 RAR 5.0 (spec 12)

The packed stream is a sequence of **blocks**, each with a byte-aligned header (spec 12.1):

```
 byte 0: flags                       byte 1: check            bytes 2..: size S (1-3 bytes, LE)
 +---+---+-------+-------+           check = 0x5A ^ flags ^ size bytes
 | T | L | nsize | bits  |           T = tables follow, L = last block of the file
 +---+---+-------+-------+           nsize = number of size bytes - 1, bits = valid bits in the last byte - 1
   7   6   5..3    2..0

 body: S bytes, holding (S - 1) * 8 + bits + 1 valid bits
```

Worked example, the first block of `rar5_compressed.rar` at offset 0x43: `ca f4 65 01`. Flags 0xCA = tables
present, last block, two size bytes, 3 valid bits in the last byte; size 0x0165 = 357; check `0x5A ^ 0xCA ^ 0x65 ^
0x01 = 0xF4`. 4 + 357 = 361 bytes, the file header's DataSize.

Tables (assigned, not added): main 306, distance 64, low distance 16, repeat length 44 (spec 12.2). Symbols
(spec 12.3):

| Symbol | Meaning |
|---|---|
| 0-255 | literal |
| 256 | filter record |
| 257 | repeat the last match |
| 258-261 | reuse D[s-258] (moved to the front), length slot from the repeat-length table |
| 262-305 | length slot s-262, then a distance; +1 / +2 / +3 when the distance is > 0x100 / 0x2000 / 0x40000; pushed |

```
length slot s:   s < 8: s + 2;   else b = s/4 - 1: 2 + ((4 | s&3) << b) + (b bits)
distance slot k: k < 4: k + 1;   else b = k/2 - 1: 1 + ((2 | k&1) << b) + low part
                 low part: b < 4: b bits;  b >= 4: ((b - 4 bits) << 4) + low-distance symbol
```

Filter records (spec 12.5): start and length as 1-4 little-endian bytes each (2 bits say how many), then 3 bits
type: 0 Delta (5 more bits: channels - 1), 1 E8, 2 E8E9, 3 ARM. Lengths are 4 bytes to 4 MiB; filters do not
overlap. RAR 5's E8 takes positions modulo 2^24; ARM rewrites the 24-bit target of every `BL` instruction (fourth
byte 0xEB) at a 4-byte boundary: `v = (v - position / 4) & 0xFFFFFF`.

Stop a block when its valid bits are used up; a symbol that runs past them is an error. After the last block
the file's stream is complete.

## III.9 RAR 7.0 (spec 12.6)

The same as RAR 5.0, except: the distance table has 80 entries, so distances go beyond 32 bits (use 64-bit
arithmetic); the dictionary may exceed 4 GiB and need not be a power of two: `128 KiB << N`, plus `(that / 32) * F`.

---

# Part IV. Encryption and integrity

## IV.1 RAR 4 (RAR 3.x AES-128), spec 6.1

Used for a file with flags 0x0004 **and** 0x0400 (salt), and for all headers when the main header has 0x0080.

```
P = password as UTF-16LE (at most 128 characters) || 8-byte salt
SHA-1 context S; iv[16]
for i = 0 .. 262143:
    S.update(P); S.update(i as 3 bytes, little-endian)
    if i % 16384 == 0: iv[i / 16384] = last byte of the SHA-1 digest of everything so far (S continues)
d = SHA-1 digest
key = d[3] d[2] d[1] d[0]  d[7] d[6] d[5] d[4]  d[11] d[10] d[9] d[8]  d[15] d[14] d[13] d[12]
```

Check with spec 6.1.1: password `pass`, salt `397d6db7a1ca0f3a` gives key `21e0fb770f14dc9881aca33844d539fe` and
IV `257613f67a8394e6da2c601041bcdcfd`. The derivation is slow on purpose: cache it per salt.

- **Encrypted data**: the data area is AES-128-CBC ciphertext of the packed stream, padded to 16 bytes. Decrypt,
  then decompress (or use, if stored), then cut at the unpacked size. RAR 4 stores no password check: a wrong
  password shows up as a broken stream or a wrong CRC.
- **Encrypted headers**: every block after the main header is stored as `salt (8) || ciphertext`, the ciphertext
  being the header padded to 16 bytes. Decrypt the first 16 bytes to learn SIZE, then the rest; check the header
  CRC. The first header failing its CRC means a wrong password. Data areas are not header-encrypted.

## IV.2 RAR 5 (AES-256), spec 6.2

```
U1 = HMAC-SHA256(password UTF-8, salt || 00 00 00 01);  Uj = HMAC-SHA256(password, U(j-1))
Key      = U1 xor ... xor U(2^n)          AES-256 key
HashKey  = U1 xor ... xor U(2^n + 16)     key for MAC'd checksums
PswCheck = U1 xor ... xor U(2^n + 32)     check[j & 7] ^= PswCheck[j] for j = 0..31 -> 8 bytes
stored check value (12 bytes) = check (8) || first 4 bytes of SHA-256(check)
```

n is the KDF count from the encryption header or record; refuse large values (more than 24 means millions of HMAC
calls). The stored check value lets a reader tell a wrong password (first 8 bytes differ) from damage (its own
SHA-256 does not match). Check with spec 6.2.2: password `pass`, n = 15, salt `2f223c211e51e3f4168a8ffae88c4e4d`
gives check value `82c76b3436bd19e1518dddfb`.

- **Encrypted data**: the file's encryption record gives salt, n and IV; AES-256-CBC as in IV.1.
- **Encrypted headers**: an encryption header (type 4) right after the signature; every later header is
  `IV (16) || ciphertext`, the header padded to 16 bytes. Each volume repeats this.
- **MAC'd checksums** (record flag 0x0002): the stored CRC32 and BLAKE2sp are not plain. Compute the plain value,
  then `mac = HMAC-SHA256(HashKey, value)` (the CRC as 4 little-endian bytes); for the CRC fold the 32 bytes to 4
  by XOR (`m[j & 3] ^= mac[j]`); for BLAKE2sp compare all 32 bytes. Example (spec 6.2.5): `noise.bin` in
  `rar5_enc_data.rar` has plain CRC `1fa12d9d` and stores `ebc748f4`.

## IV.3 The order of operations for one file

```
data areas of all parts --> AES-CBC decrypt (one chain) --> decompress (or stored) --> cut to size
                                                                                   \--> CRC-32 / BLAKE2sp
                                                                                        (MAC'd if flagged)
                                                                                        compare with header
```

---

# Part V. Writing a reader

## V.1 Architecture

```
      +--------------------------------------------------------------+
      |  open(volumes, password)                                     |
      |   signature search -> header loop (RAR 4 or RAR 5)           |
      |   -> entries: name, sizes, method, dictionary, solid,        |
      |      encryption parameters, checksums, list of pieces         |
      +--------------------------------------------------------------+
                         |
                         v  read(entry k)
      +--------------------------------------------------------------+
      |  solid? decode the chain before k (discarding), unless the   |
      |  decoder already stopped right before k                      |
      |  byte source: pieces in order -> AES-CBC (if encrypted)      |
      |  decoder: stored | RAR 2.0 | RAR 2.9 | RAR 5/7               |
      |  sink: cut at size, CRC-32 (+ BLAKE2sp), hand bytes out       |
      +--------------------------------------------------------------+
```

Rubraview's reader follows this shape: `src/core/rar.c` (headers, volumes, passwords, solid chains) and
`src/core/rarcodec/` (`crypto.c`, `unpack.c` for the shared machinery, `unpack20.c`, `unpack29.c`,
`unpack50.c`, `filters.c`), behind the table in `include/rubraview/rar_codec.h`.

## V.2 Pseudo-code of the header loop

```
find signature (first MiB) -> format 4 or 5
RAR 4:
  pos = after signature
  while at least 7 bytes remain:
      if headers encrypted: salt = 8 bytes; decrypt one header
      read 7-byte block header, check CRC
      main (0x73): note volume / solid / naming / header encryption
      file (0x74): parse (II.2); unless a directory or a link, record it with its data area as a piece;
                   a part with "split before" adds a piece to the file it continues
      end (0x7B): note "more volumes"; stop
      other: skip SIZE (+ ADD_SIZE)
RAR 5:
  same, with the frame of II.3: type 4 derives the header key, 1 checks the volume number,
  2 records a file, 3 is skipped, 5 ends the volume
next volume: repeat from its signature while a file is still split or the end header said "more"
```

## V.3 Limits and robustness

Fuzzed archives (`rar_invalid1.rar`, `rar_ppmd_use_after_free.rar`, `rar5_leftshift1.rar`,
`rar5_readtables_overflow.rar`, `rar5_truncated_huff.rar`) must give errors, never crashes or endless loops.
Check every length against the bytes that remain **before** using it, and every count against these limits
(spec 14):

| Item | Limit |
|---|---|
| RAR 4 header size | 7..65535, file header >= 32 |
| RAR 5 header size | size vint at most 3 bytes (2 MiB) |
| vint | at most 10 bytes |
| dictionary | refuse above a configured limit before allocating |
| RAR 5 KDF count | <= 24 |
| RAR 3 programs / pending filters | 1024 / 8192 |
| RAR 3 program code | 1..65536 bytes; global data <= 0x2000 - 0x40 |
| RAR 3 filter block | <= 0x3C000 (E8/E8E9, > 4), <= 0x1E000 (Delta, RGB, Audio) |
| RAR 5 filter block | 4 .. 0x400000 |
| PPMd | order 2..64, memory 1..256 MiB |
| password | 128 characters |

Other habits that pay: treat a Huffman code no symbol has as an error; never let a distance read outside the
window; stop when the packed data is used up; build with AddressSanitizer and UndefinedBehaviorSanitizer while
testing, so that an out-of-bounds read on a damaged archive fails loudly instead of passing by luck.

## V.4 Testing with oracles

A decoder is only proven by data it did not make. The fixtures in `tests/fixtures/rar/` come with **manifests**:
other programs' output, one line per file, `archive|entry|size|crc32`:

- `expected.txt`: libarchive 3.7.7's bsdtar on libarchive's own test archives;
- `expected-unrar.txt`, `expected-oldrar.txt`: RARLAB's UnRAR (run, not read) on archives made with RARLAB's
  programs from generated inputs (`make-oldrar-inputs.py` makes the inputs again, byte for byte).

`tests/test_rar.c` reads every line, extracts the entry by streaming it into a CRC-32, and compares. It also reads
solid archives out of order (the last file first, then all in order) so that both "start the chain again" and
"continue the chain" are exercised. Work in small steps (stored files, then volumes, then one algorithm at a
time) and run everything after each step; a failing fixture then points at the newest code.

---

# Part VI. How this knowledge was obtained, and how to do the same

## VI.1 What happened here

No archiver was disassembled or traced, and no restricted source code was read. The work was split between two
sessions that never shared anything but documents (a **clean-room** process):

```
 open sources and published documents                 standards (FIPS, RFC)
 (libarchive BSD-2, rardecode BSD-2,                          |
  RARLAB's published RAR 5 technote,                          |
  public-domain PPMd)                                         |
        |                                                     |
        v                                                     |
 +----------------------+   only the written spec   +--------------------------+
 | rar-spec session     | ------------------------> | rar-decoder session      |
 | reads the sources,   |   (and answers to         | reads the spec, the      |
 | writes the spec in   |    questions, as commits) | standards, the fixtures; |
 | its own words, names |                           | writes the code and the  |
 | a source per fact    |                           | tests; never sees the    |
 +----------------------+                           | sources the spec came    |
                                                    | from                     |
                                                    +--------------------------+
 test archives: libarchive's test suite (BSD-2), and RARLAB's own programs run as black boxes on generated
 inputs; their output (what files come out, with sizes and CRCs) recorded as manifests
```

- The spec's Sources table and `docs/cleanroom/README.md` say what the `rar-spec` session read and what it was
  forbidden to read (UnRAR in any form, among others).
- `docs/cleanroom/provenance-rar-decoder.md` lists every file the decoder session opened, why, and the evidence:
  the git history of its copy (which starts from a single baseline commit without the forbidden files), the
  manifests it tested against, and the standard test vectors.
- The decoder session's fixture verdicts on the spec's open points went back to the spec side in writing
  (`docs/cleanroom/questions-rar-decoder.md`).

These records describe the method; they are not legal advice. Whether a particular use is permitted depends on
licences and law, which the owner decides.

## VI.2 Doing the same for another format

1. **Decide what is allowed.** List the sources whose licences allow learning from them and redistributing what
   you write (e.g. MIT, BSD, public domain, published format documents), and those you must not touch (code under
   restrictive licences, leaked material). Write both lists down before anyone starts.
2. **Separate the roles.** The person who reads sources writes a specification in their own words, citing a
   source for every fact; another person, who has not read those sources, implements from the specification only.
   Each starts from a clean copy that physically lacks the forbidden files.
3. **Make test data you are allowed to use.** Openly licensed test suites, and archives you create yourself with
   the real tools from inputs you generate. Use the tools as **black boxes**: run them, keep their inputs and
   outputs, never open their insides.
4. **Use oracles, not code.** What a reference tool extracts (names, sizes, checksums) is a fact about the data;
   record it in manifests and test against them.
5. **Questions go in writing.** When the specification is not enough, the implementer writes the question down;
   the specification side answers by changing the specification.
6. **Keep the evidence.** Version control from the first commit, every file headed with what it was written
   from, a list of every input opened and why, and the final test results.

Learning a format by observing what a tool does with chosen inputs (step 3) is a standard technique, but its
legality depends on the tool's licence and on local law; check both before doing it.

---

# Appendix. Tables at a glance

## A.1 RAR 2.0 / 2.9 base tables (spec 9.3)

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

## A.2 Table sizes

| Algorithm | Main | Distance | Low distance | Length / repeat | Total lengths sent |
|---|---|---|---|---|---|
| RAR 2.0 LZ | 298 | 48 | - | 28 | 374 |
| RAR 2.0 audio | 257 per channel (1-4) | | | | 257 * channels |
| RAR 2.9 | 299 | 60 | 17 | 28 | 404 |
| RAR 5.0 | 306 | 64 | 16 | 44 | 430 |
| RAR 7.0 | 306 | 80 | 16 | 44 | 446 |

## A.3 What the fixtures confirmed

Points where the spec's sources disagreed or only one source spoke. The spec's recommended reading was
implemented for each, and every fixture decodes under it, so none needed the alternative. That shows the
readings are consistent with the data, not that every fixture exercises every point. The readings: the PPMd
escape byte is kept between blocks; `lowDist`/`lowRepeat` reset at every LZ table read; distances reset at a
non-solid file; RAR 2.0 pushes reused distances; RAR 2.0 solid files continue mid-block. Observed directly: RAR
3.93 wrote the Itanium filter and filters inside PPMd blocks, and they decode as described; BLAKE2sp matches real
data; RAR 7 distances need 64 bits. No fixture contains a RAR 5 E8 or E8E9 filter. Details: `docs/cleanroom/questions-rar-decoder.md`.
