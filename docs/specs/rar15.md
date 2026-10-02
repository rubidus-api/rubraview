<!-- Spec: RAR 1.5 compression (unpack version 15), for implementers. Written in the rar15-blackbox clean room from
the behaviour of RAR 1.55 for DOS (run as a black box in DOSBox), the step log docs/cleanroom/log-rar15-blackbox.md,
and docs/specs/rar-decompression.md (Conventions, sections 1, 2 and 5.1). -->

# RAR 1.5 compression (unpack version 15)

This document says how to decode the files that RAR 1.5x writes with `UNP_VER` 15 and methods 0x31 to 0x35. It
is enough to write a decoder; it says nothing about how to compress.

Each rule has a reference `[Lnnnn]` to the step log `docs/cleanroom/log-rar15-blackbox.md`. A marker after the
reference gives the source of the rule:

- `obs`: found from RAR 1.55's output in the cited steps.
- `prior, confirmed`: first written from the session's general prior knowledge, then tested against RAR 1.55
  in the cited steps. See "Method and its limits".

## 1. Method and its limits

RAR 1.55 for DOS (released 20 August 1995) was run in DOSBox and was only ever used in two directions [L0001-L0007]:

1. RAR compressed inputs that we chose (`tests/fixtures/rar15/make-inputs.py`), and we read the archives it wrote.
2. RAR extracted archives that we built around packed streams of our own (`make-probes.py`), and we read what it
   wrote out. RAR keeps the extracted file even when its CRC fails, which makes it a decoder oracle [L0007].

No RAR program was disassembled or read inside, and no text that describes this format was consulted. The decoder
model in `tools/rar15_model.py` agrees with RAR 1.55 on every output recorded: 5478 probe streams
(`tests/fixtures/rar15/runs/`, checked by `tools/rar15_verify_runs.py`) and all 74 entries of the 27 fixture
archives [L0024, L0029, L0031-L0034].

**Limitation (the owner's decision Q1, `docs/cleanroom/questions-rar15-blackbox.md`).** This session is a language
model whose training data very likely contains UnRAR's source and other descriptions of this format. The overall
structure of the decoder (section 6 onwards) and the starting values of its tables came in as a recalled
hypothesis [L0012]. Each part was then tested against RAR 1.55, and the tests found and corrected four places
where RAR 1.55 behaves differently (sections 4.3, 9.4, 9.5 and 6.3). The log cannot show that choosing what to test
was free of that prior knowledge. Every rule is marked with where it came from.

## 2. Container

The archive is the RAR 4 layout of `rar-decompression.md` section 2. RAR 1.55 writes the following
[L0005, L0008, L0009]:

- Signature, then a main header (0x73, `HEAD_SIZE` 13). Main flags seen: 0x0001 (volume), 0x0008 (solid). No
  0x0100 (first volume) is set, even on the first volume. There is no end-of-archive block (0x7B).
- File headers (0x74) with `HOST_OS` 0, `UNP_VER` 15, attributes 0x20, no extended time. The dictionary bits of the
  file flags (0x00E0) are 0: **the window is 64 KiB**.
- `METHOD` 0x30 = stored (the data area is the file). 0x31..0x35 (`-m1`..`-m5`) = compressed. All five use the same
  stream format; for a small input they even give the same bytes [L0006]. A file that does not shrink is stored
  (0x30) in a non-solid archive. In a solid archive every file is compressed, even if it gets larger [L0005, L0008].
- An empty file is compressed with `PACK_SIZE` 0 and `UNP_SIZE` 0. Decoding it reads nothing [L0005, L0029].
- **Solid archives**: only the main flag 0x0008 says so. The file flag 0x0010 is never set, and every file after
  the first continues the decoder state (section 6.3) [L0008, L0009, L0028]. RAR sorts the files of a solid
  archive by extension unless `-ds` is given. A decoder does not need to care: it simply decodes in archive order.
  In a solid archive RAR 1.55 compresses every file, even a 1-byte one (3 packed bytes), so stored files do not
  occur there [L0008, L0029]. A decoder that meets one should leave the decoder state alone (not observed).
- **Volumes**: names `.RAR`, `.R00`, `.R01`, .... A file split over volumes has flag 0x0002 on its first part and
  0x0001 on the last (both on middle parts). Every part repeats the whole file's `UNP_SIZE`. The packed stream is
  the concatenation of the parts' data areas. `FILE_CRC` is FFFFFFFF on all but the last part; the last part holds
  the CRC-32 (`rar-decompression.md` 5.1) of the whole file [L0008, L0009, L0029].
- `FILE_CRC` is the CRC-32 of the unpacked file [L0005, L0033].

Not examined (out of scope or not met): encryption, comments, directories, authenticity information, unknown
unpacked size.

## 3. Bit input

The packed stream is read most significant bit first (`rar-decompression.md`, Conventions).

- `peek16()`: the next 16 bits as an unsigned number, the first bit being the most significant. It does not consume
  them.
- `skip(n)`: consume n bits.

A decoder may look ahead past the end of the data area while peeking. Bits there read as 0. RAR's own streams never
*consume* a bit past their data area. They end on a token boundary and pad the last byte with 0 to 8 unused bits
[L0034]. A stream that does consume past its end is corrupt. RAR itself does not need the last byte when it holds
no used bits [L0010].

## 4. Prefix codes

### 4.1 Code tables

Every variable-length number is read with one of seven canonical prefix codes. A code table is given by its
shortest length and by the number of codes of each length. Values are assigned in order: the shortest codes get the
smallest values, and within one length the numerically smaller code gets the smaller value. Within a length, codes
are consecutive binary numbers that start right after the last code of the previous length, shifted left by the
length difference (the usual canonical assignment). Every table is a complete code.

| Table | Codes per length (length: count) | Values |
|---|---|---|
| P0 | 4:8, 5:8, 6:8, 7:9, 12:224 | 0..256 |
| P1 | 5:4, 6:40, 7:16, 8:16, 9:4, 11:47, 12:130 | 0..256 |
| P2 | 5:2, 6:5, 7:46, 8:64, 9:116, 10:24 | 0..256 |
| P3 | 6:2, 7:14, 8:202, 9:33, 10:6 | 0..256 |
| P4 | 8:255, 9:2 | 0..256 |
| N1 | 2:2, 3:1, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 12:224 | 0..255 |
| N2 | 3:5, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 11:2, 12:220 | 0..255 |

For example P1: values 0-3 are `00000`..`00011`, values 4-43 are `000100`..`101111`, values 44-59 are
`1100000`..`1101111`, and so on. The last code of P0..P4 (all ones: 12 bits in P0 and P1, 10 in P2 and P3, 9 in P4)
has value 256.

To decode a value with table T: take `b = peek16()`. Find the length L whose code range contains the top L bits of
b, then `skip(L)`. The value is the code's value. All codes are 12 bits or shorter, so 16 bits of look-ahead are
always enough.

Sources: first written from prior knowledge as threshold tables [L0012]. Every value of every table was then
decoded at least 15 times, in streams that RAR 1.55 decodes the same way [L0032] (`prior, confirmed`). The first
codes of P1 (values 0, 4, 12, 20, 28, 36, 44, 60) and of P2 (flags, values 0..6) are also columns of the
first-byte sweep [L0011, L0013] (`obs`).

### 4.2 Where each table is used

| Use | Table |
|---|---|
| flags byte (section 7) | P2 |
| literal (section 8) | P0..P4, chosen by the literal average |
| stored-mode match distance (8.2) | P2 |
| short match distance (9.3) | P2 |
| long match length (10.1) | N1 or N2, chosen by the long-length average |
| long match distance (10.2) | P0, P1 or P2, chosen by the distance average |
| old-distance length, far length (9.4, 9.5) | N1, N2 |

### 4.3 The code past the last value (value 256 of P0..P4)

The all-ones code of P0..P4 (value 256) is decoded in two ways [L0016, L0027] (`obs`; the prior expectation was
"full length, value 256 used as 0", which RAR 1.55 refutes):

- **Outside the stored mode** (section 8.2): only the table's *shortest* length is consumed (4, 5, 5, 6 or 8 bits
  for P0..P4), and the value is 0. The remaining bits of that code are read again as the start of the next code.
  Observed for flags (P2), literals (P1, P2, P3, P4), short-match distances (P2) and long-match distances (P0, P1,
  P2) [L0016, L0018, L0031, L0035]; P0 as a literal table is assumed to behave the same (not met).
- **In the stored mode**: the whole code is consumed and the value is 256. RAR's own compressor uses it there
  (section 8.2) [L0027].

## 5. Order tables

A decoded value is usually a *rank*. An order table turns the rank into the symbol and then moves that symbol up.
Four order tables are used:

| Name | Holds | Entries |
|---|---|---|
| literal order LO[256] | byte values | 16-bit: symbol << 8 \| weight |
| far order FO[256] | high bytes of long-match distances | 16-bit: symbol << 8 \| weight |
| flag order GO[256] | flags bytes | 16-bit: symbol << 8 \| weight |
| short order SO[256] | short-match distances - 1 | plain 0..255 |

LO, FO and GO each have a start array: LS, FS and GS[256], unsigned counters.

**Regroup(O, S)**: give entries 0..31 weight 7, entries 32..63 weight 6, and so on down to entries 224..255 weight 0,
keeping each symbol. Then clear S and set S[w] = (7 - w) * 32 for w = 0..6. This makes S[w] the position of the
first entry of weight w.

**Promote(O, S, r, limit)** moves the symbol at rank r:

```
loop:
    e = O[r]
    q = S[e & 0xFF];  S[e & 0xFF] += 1          # first position of e's weight group
    e = e + 1                                    # weight + 1
    if limit(e & 0xFF): Regroup(O, S); continue  # weights overflowed: regroup and redo from O[r]
    break
O[r] = O[q]
O[q] = e
```

`limit(w)` is `w > 0xA1` for LO, and `w == 0` (the weight wrapped from 0xFF) for FO and GO. The carry out of the
weight byte is never stored, because that case always regroups and reads O[r] again [L0034]. The rules for SO are in
section 9.3.

Initial contents [L0012] (`prior, confirmed` by every probe that starts from a fresh state, e.g. [L0011, L0013]):

- LO[i] = i << 8, LS all 0.
- FO[i] = i << 8, then Regroup(FO, FS).
- GO[i] = ((256 - i) & 0xFF) << 8 (so GO[0] = 0x0000, GO[1] = 0xFF00, GO[2] = 0xFE00, ...), GS all 0.
- SO[i] = i.

## 6. Decoder state

### 6.1 Variables

| Name | Start value | Meaning |
|---|---|---|
| window[65536], pos | zeros, 0 | the last 64 KiB of output; pos wraps at 65536 |
| litavg | 0x3500 | literal average: chooses the literal table |
| distavg | 0 | distance average: chooses the long-distance table |
| shortavg | 0 | short-length average: chooses the short-code table |
| longavg | 0 | long-length average: chooses the long-length table |
| repavg | 0 | near-repeat average (section 10.3) |
| litrun | 0 | literals since the last match (enters the stored mode) |
| litw, matchw | 0x80, 0x80 | literal and match weights: choose what a flag bit means |
| farlimit | 0x2001 | distance from which matches get one byte longer |
| fartoggle | 0 | selects one bit of the short-code tables (9.2) |
| stored | 0 | the stored mode (8.2) |
| repcount | 0 | the repeat counter (9.1) |
| old[4], oldi | 0, 0 | the last four match distances, written round-robin |
| lastdist, lastlen | 0, 0 | the last match (for "repeat last match") |
| flags, flagcnt | 0, 0 | current flags byte and how many of its bits are left |

All averages are unsigned and stay below 65536.

### 6.2 Decoding one file

```
decode_file(stream, size, continue_solid):
    if not continue_solid: set every variable and order table to its start value
    stored = 0                                   # [L0028]
    left = size - 1                              # signed
    if left >= 0: flags = read_flags(); flagcnt = 8
    while left >= 0: decode_token()
    output: the first `size` bytes written during this call
```

`copy` and `put` (section 11) decrease `left` by the number of bytes they write. A token is always completed, even
when it runs past `size`. The extra bytes go into the window and are not output. RAR's own archives never do this:
every file ends exactly at `left = -1` [L0034]. RAR 1.55 itself writes such extra bytes out [L0007, L0010], which is
not part of the format.

### 6.3 Solid archives

Each file after the first is decoded with `continue_solid` set: window, pos, all averages and weights, the order
tables, old distances, last match, repcount, fartoggle and litrun carry over. Only `stored` is cleared, and a new
flags byte is read [L0028] (`obs`: keeping the stored mode, skipping the flags read, or also clearing litrun each
make RAR's solid archives decode wrongly). A file's bit input starts at its own data area (section 3). An empty
file reads nothing and changes nothing except `stored` [L0029].

## 7. Flags and token choice [L0012, L0013, L0015] (`prior, confirmed`)

```
read_flags():
    r = decode(P2) & 0xFF                        # value 256 cannot occur here (4.3)
    loop:                                        # Promote with the flags byte kept
        e = GO[r]; f = e >> 8
        q = GS[e & 0xFF]; GS[e & 0xFF] += 1; e = e + 1
        if (e & 0xFF) == 0: Regroup(GO, GS); continue
        break
    GO[r] = GO[q]; GO[q] = e
    return f

decode_token():
    if stored: literal(); return
    flagcnt -= 1; if flagcnt < 0: flags = read_flags(); flagcnt = 7
    if flags & 0x80:
        flags = (flags << 1) & 0xFF
        if matchw > litw: long_match() else literal()
    else:
        flags = (flags << 1) & 0xFF
        flagcnt -= 1; if flagcnt < 0: flags = read_flags(); flagcnt = 7
        if flags & 0x80:
            flags = (flags << 1) & 0xFF
            if matchw > litw: literal() else long_match()
        else:
            flags = (flags << 1) & 0xFF
            short_match()
```

So `1` and `01` mean "literal" and "long match", in an order set by the two weights, and `00` means "short match".

## 8. Literals

### 8.1 Normal literals [L0012, L0013, L0015, L0032] (`prior, confirmed`)

```
literal():
    b = peek16()
    T = P4 if litavg > 0x75FF else P3 if litavg > 0x5DFF else P2 if litavg > 0x35FF
        else P1 if litavg > 0x0DFF else P0
    r = decode(T) & 0xFF
    if stored: (section 8.2, which may return here with r changed, or end the token)
    else:
        litrun += 1
        if litrun - 1 >= 16 and flagcnt == 0: stored = 1      # the 17th literal in a row, at a flags byte end
    litavg = litavg + r;  litavg = litavg - (litavg >> 8)
    litw += 16; if litw > 0xFF: litw = 0x90; matchw = matchw >> 1
    put(LO[r] >> 8)
    Promote(LO, LS, r, w > 0xA1)
```

### 8.2 The stored mode [L0012, L0027, L0028] (`prior, confirmed`)

In the stored mode no flags are read: every token is a literal code. The rank is shifted by one so that rank code
0 can serve as an escape:

```
    (in literal(), when stored == 1, after r = decode(T) & 0xFF, using the b peeked before it)
    if r == 0 and b > 0x0FFF: r = 256           # the all-ones code (4.3, read at full length)
    r = r - 1
    if r >= 0: continue as in 8.1 (litavg, weights, put, Promote) with this r
    else (escape):
        b = peek16(); skip(1)
        if b & 0x8000: litrun = 0; stored = 0; return        # leave the stored mode
        len = 4 if b & 0x4000 else 3; skip(1)
        d = decode(P2)
        d = (d << 5) | (peek16() >> 11); skip(5)
        copy(d, len)                                         # a plain copy (section 11)
        return
```

`b > 0x0FFF` tells the all-ones code (value 256, masked to 0) from the real code of value 0, which is at least four
zero bits in every table (so b <= 0x0FFF).

## 9. Short matches

### 9.1 Repeat counter [L0022, L0023] (`prior, confirmed`; the 0-bit rule `obs`)

```
short_match():
    litrun = 0
    b = peek16()
    if repcount == 2:
        skip(1)
        if b & 0x8000: copy(lastdist, lastlen); return       # repeat the last match again
        b = (b << 1) & 0xFFFF                                # repcount is NOT reset here [L0023]
    c = b >> 8                                               # the next 8 bits
    ...section 9.2
```

The prior expectation was that the 0 bit resets `repcount` to 0. RAR 1.55 keeps it. A following "repeat last
match" code then makes it 3, 4, ..., which no longer triggers the extra bit. Any other short code sets it to 0 [L0023].

### 9.2 Short codes [L0012, L0019, L0032] (`prior, confirmed`)

Table S1 is used when shortavg < 37, S2 otherwise. Entries are tried in order k = 0, 1, ..., 14. The first whose
code is a prefix of the 8 bits `c` is taken, and its length is skipped. One entry has length 3 + fartoggle: its code
is `101` when fartoggle = 0 and `1010` when it is 1. While fartoggle = 0 that 3-bit code shadows entry 14 (`1011`),
so entry 14 then never occurs [L0032].

| k | S1 code | S2 code | Meaning |
|---|---|---|---|
| 0 | `0` | `00` | new short match, length 2 |
| 1 | `101` / `1010` (toggle) | `010` | length 3 |
| 2 | `1101` | `011` | length 4 |
| 3 | `1110` | `101` / `1010` (toggle) | length 5 |
| 4 | `11110` | `1101` | length 6 |
| 5 | `111110` | `1110` | length 7 |
| 6 | `1111110` | `11110` | length 8 |
| 7 | `11111110` | `111110` | length 9 |
| 8 | `11111111` | `111111` | length 10 |
| 9 | `1100` | `1100` | repeat last match |
| 10 | `1000` | `1000` | old distance 1 (the most recent) |
| 11 | `10010` | `10010` | old distance 2 |
| 12 | `100110` | `100110` | old distance 3 |
| 13 | `100111` | `100111` | old distance 4 |
| 14 | `1011` | `1011` | far match |

### 9.3 New short match (k = 0..8)

```
    repcount = 0
    shortavg = shortavg + k; shortavg = shortavg - (shortavg >> 4)
    r = decode(P2) & 0xFF
    d = SO[r]
    if r > 0: SO[r] = SO[r - 1]; SO[r - 1] = d                # move up by one place
    len = k + 2; d = d + 1
    old[oldi] = d; oldi = (oldi + 1) & 3
    lastlen = len; lastdist = d
    copy(d, len)
```

### 9.4 Repeat and old distances (k = 9..13)

```
    k == 9:  repcount += 1; copy(lastdist, lastlen); return   # plain copy: old[], last* unchanged [L0017]
    k = 10..13:
        repcount = 0
        d = old[(oldi - (k - 9)) & 3]
        len = decode(N1) + 2
        if len == 0x101 and k == 10: fartoggle ^= 1; return  # a toggle, no output
        len = len & 0xFF                                     # [L0020] (obs)
        if d > 256: len += 1
        if d >= farlimit: len += 1
        old[oldi] = d; oldi = (oldi + 1) & 3
        lastlen = len; lastdist = d
        copy(d, len)
```

`len & 0xFF`: the base length N1 + 2 is kept in 8 bits after the toggle test. So N1 value 254 gives 0 (nothing is
copied, the distance still goes into old[] and lastdist) and value 255 with k = 11..13 gives 1. The distance
increments come after the mask [L0020] (`obs`; the prior expectation had no mask).

### 9.5 Far match (k = 14)

```
    repcount = 0
    len = decode(N2) + 5
    d = (peek16() >> 1) | 0x8000; skip(15)
    lastlen = len; lastdist = d
    copy(d, len)                                             # plain copy: old[] unchanged [L0017]
```

## 10. Long matches [L0012, L0015, L0023, L0031, L0032] (`prior, confirmed`)

### 10.1 Length

```
long_match():
    litrun = 0
    matchw += 16; if matchw > 0xFF: matchw = 0x90; litw = litw >> 1
    oldlong = longavg
    b = peek16()
    if longavg >= 122:  len = decode(N2)
    elif longavg >= 64: len = decode(N1)
    elif b < 0x100:     len = b; skip(16)
    else:               len = number of 0 bits before the first 1 bit of b; skip(len + 1)
    longavg = longavg + len; longavg = longavg - (longavg >> 5)
```

### 10.2 Distance

```
    T = P2 if distavg > 0x28FF else P1 if distavg > 0x06FF else P0
    r = decode(T)
    distavg = distavg + r; distavg = distavg - (distavg >> 8)
    loop:                                                    # Promote(FO, FS, r & 0xFF, w == 0), keeping e
        e = FO[r & 0xFF]
        q = FS[e & 0xFF]; FS[e & 0xFF] += 1; e = e + 1
        if (e & 0xFF) == 0: Regroup(FO, FS); continue
        break
    FO[r & 0xFF] = FO[q]; FO[q] = e
    d = ((e & 0xFF00) | (peek16() >> 8)) >> 1; skip(7)       # high byte from FO, 7 more bits
```

### 10.3 Length corrections and farlimit

```
    oldrep = repavg
    if len != 1 and len != 4:
        if len == 0 and d <= farlimit: repavg += 1; repavg = repavg - (repavg >> 8)
        elif repavg > 0: repavg -= 1
    len += 3
    if d >= farlimit: len += 1
    if d <= 256: len += 8
    farlimit = 0x7F00 if (oldrep > 0xB0 or (litavg >= 0x2A00 and oldlong < 0x40)) else 0x2001
    old[oldi] = d; oldi = (oldi + 1) & 3
    lastlen = len; lastdist = d
    copy(d, len)
```

Unlike the old-distance path, this length is not reduced to 8 bits. Masking it makes RAR's streams decode wrongly
[L0020].

## 11. Output

```
put(byte):     window[pos] = byte; pos = (pos + 1) & 0xFFFF; left -= 1
copy(d, len):  repeat len times: window[pos] = window[(pos - d) & 0xFFFF]; pos = (pos + 1) & 0xFFFF
               left -= len
```

The copy goes byte by byte, so d < len repeats a pattern. d = 0 reads the byte being written (the window's old
content at pos). A decoder outputs exactly `UNP_SIZE` bytes per file (6.2).

## 12. Test data

- `tests/fixtures/rar15/arc/`: 27 archive files made by RAR 1.55 (stored, all levels, solid, volumes, window wrap,
  stored mode, far distances). Their entries' sizes and CRC-32 come from RAR 1.55's own extraction:
  `expected-rar15.txt` [L0033].
- `tests/fixtures/rar15/runs/*.json`: 5478 probe streams with RAR 1.55's output. They cover sections 4.3, 9.1 and
  9.4, which RAR's own archives rarely or never reach [L0015-L0024, L0031].
- `tests/fixtures/rar15/probes-rar15.txt`: a subset with outputs, replayed by the C decoder's tests.

## 13. Rules and evidence

| Rule | Section | Steps | Source |
|---|---|---|---|
| container fields, stored fallback, empty files | 2 | L0005, L0029 | obs |
| same stream for -m1..-m5 | 2 | L0006 | obs |
| solid via main flag only; state carries over | 2, 6.3 | L0008, L0009, L0028 | obs |
| volumes: joined data areas, CRC on the last part | 2 | L0008, L0009, L0029 | obs |
| MSB-first bits, token-boundary file ends | 3 | L0010, L0034 | obs |
| code tables P0..P4, N1, N2 | 4.1 | L0012, L0032 (L0011, L0013) | prior, confirmed |
| code past the last value: short outside stored mode | 4.3 | L0016, L0018, L0031, L0035 | obs |
| ... full length in stored mode | 4.3, 8.2 | L0027 | obs |
| order tables, Regroup, Promote, start contents | 5 | L0012, L0013, L0015-L0024, L0031 | prior, confirmed |
| flags and token choice | 7 | L0012, L0013, L0015 | prior, confirmed |
| literal table choice, litavg, weights, stored mode entry | 8.1 | L0012, L0015, L0026-L0031 | prior, confirmed |
| stored mode escape | 8.2 | L0012, L0026-L0029 | prior, confirmed |
| repeat counter extra bit | 9.1 | L0022 | prior, confirmed |
| repeat counter kept after the 0 bit | 9.1 | L0023 | obs |
| short codes S1, S2 and the toggle bit | 9.2 | L0019, L0032 | prior, confirmed |
| plain copies do not touch old[] | 9.4, 9.5, 8.2 | L0017 | prior, confirmed |
| old-distance base length in 8 bits | 9.4 | L0020 | obs |
| long matches | 10 | L0012, L0015, L0023, L0026, L0031 | prior, confirmed |
| solid file start: stored mode cleared, new flags byte | 6.3 | L0028 | obs |
