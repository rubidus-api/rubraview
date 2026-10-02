<!-- Sources: earlier independent black-box notes below; continuation evidence in docs/cleanroom/azo-2026-10-02/. -->
# AZO compression (EGG method 3)

Written 2026-10-01 by the `azo-blackbox` clean-room session, **from ALZip's output alone**: EGG archives that
ALZip 8.6 (Korean) made on the project's Windows 11 VM from inputs chosen for this work. No AZO or EGG
implementation was read, and no ESTsoft binary was opened, disassembled or traced. Other sources:
`docs/specs/egg-format.md` (the container, from the `egg-container` session), the LZMA SDK's public
`DOC/lzma-specification.txt` (LZMA SDK 23.01, public domain), used only as a hypothesis to test, and namu.wiki/w/AZO.

Every statement names the sample (under `tests/fixtures/egg/azo/`) that shows it. Numbers are hexadecimal
where they start with `0x` or are shown as bytes. **Unobserved** marks what no sample showed; a reader
should refuse it.

## 2026-10-02 round 3 result (authoritative)

The authoritative decoding rules are now
[the round-3 decoding model](../cleanroom/azo-2026-10-02-round3/decoding-model.md) and its
[reference decoder](../cleanroom/azo-2026-10-02-round3/reference_decoder.py); the evidence summary is
[the round-3 README](../cleanroom/azo-2026-10-02-round3/README.md). Where the text below disagrees with them,
they win. The reference decoder reproduces all 18809 retained pairs byte for byte with EGG CRC (18764
compressed AZO, 45 stored/empty controls), including six inputs compressed after the model was frozen.

Scope: this validates the observed ALZip 8.6 stream profile only, not every AZO variant. Counter saturation in
the vendor coder is still unidentified (the reference uses unbounded counters). There is no production C decoder
and no EGG-reader integration yet. The text below preserves earlier hypotheses and historical source-use
statements, and the 2026-10-02 round-2 [continuation notes](azo-decoding-notes-2026-10-02.md) are superseded
by round 3.

## 1. How to make AZO samples

ALZip picks the method per file by extension. With 최적압축 (`ALZipCon -a -m4`), ALZip 8.6 writes method 3
for files named `*.com` or `*.sys` and never otherwise (namu.wiki/w/AZO: "파일의 확장자를 .com 으로 바꾸고 egg
포맷으로 압축을 해보면 실험이 가능하다"). Evidence:

- `alzip86_com/com__*__m4.egg`, `sys__*__m4.egg`: every non-empty input of more than one byte has a method 3
  block, hint byte 1. The one-byte input is stored (method 0); the empty input has no block.
- `alzip86_com/*__m3.egg` (`-m3` on the same `.com` / `.sys` files): method 4 (LZMA).
- The same inputs named `.bin` / `.txt` at `-m0`..`-m4` (the egg-container session's 8.6 set): never method 3.

Method 3 data is the "packed bytes" of an EGG block (`egg-format.md` section 5). Encryption, when present,
wraps the whole AZO stream like any other method (`com__ALL__m4_pw.egg`: packed 71 bytes, the same size as
unencrypted `com__04_short_text__m4.egg`).

## 2. Stream layout

```text
stream header   2 bytes: 31 00
block           12-byte block header, then its data      (zero or more)
...
end             12 zero bytes
```

All numbers in the AZO stream are **big-endian** (unlike the EGG container).

### 2.1 Stream header

`31 00` at the start of every method 3 block in every sample (`com__*`, `sys__*`). Other values are
**unobserved**.

### 2.2 Block header (12 bytes)

| Offset | Size | Field                              |
|--------|------|------------------------------------|
| 0      | 4    | unpacked size of this block (u32 BE) |
| 4      | 4    | packed size of this block's data (u32 BE) |
| 8      | 4    | check = unpacked XOR packed        |

Evidence: `com__05_text_twice__m4.egg` block at 0x64: `31 00 | 00 00 00 5a | 00 00 00 31 | 00 00 00 6b`:
unpacked 90 (the input's size), packed 49, and 0x5a XOR 0x31 = 0x6b. The check holds in every block of every
sample.

The stream ends with a header whose unpacked and packed sizes are 0 (12 zero bytes): it is the last 12 bytes of
every sample's payload, and the EGG block's packed size counts it (`com__02_a_x000002__m4.egg`: 2 + 12 + 2 + 12
= 28).

### 2.3 Block sizes

Input is cut into blocks of 524 288 bytes (512 KiB); the last block holds the rest:
`com__07_random_1m__m4.egg` has two blocks of 0x80000; `com__08_text_large__m4.egg` (1 098 300 bytes) has
0x80000, 0x80000, 0xC23C.

### 2.4 Stored blocks

When packed size = unpacked size the block's data is the input bytes unchanged: `com__04_short_text__m4.egg`
(45, 45: the data is `The quick brown fox...`), `com__07_random_64k__m4.egg` (65 536, 65 536),
`com__06_counting__m4.egg` (256, 256). ALZip also stores blocks it could have shrunk only slightly (`a` × 8 is
stored, `a` × 16 packs to 3 bytes), so a reader must use the rule "equal sizes ⇒ stored" and nothing else.

### 2.5 Compressed blocks

Each compressed block is decoded on its own (section 3): its first data byte is the block's first output byte,
unchanged (`com__08_text_large__m4.egg`: blocks 2 and 3 start with `72`, the input's bytes at 524 288 and
1 048 576).

## 3. Compressed block data

Status (2026-10-02): the arithmetic coder, the literal model, the symbol grammar and most contexts are measured;
section 3.8 lists what is still open. `tools/azo/azo3.py` implements this section. It decodes every sample of
`alzip86_p3`..`p7`, `p10`, `p11`, `p13`..`p15`, `p17`, most of `p2`, `p8`, `p9`, `p12`, `p16`, and 32 of the 40
`alzip86_com` streams; what fails is listed in 3.8.

Sources added for this section: black-box runs of ALZip as a decoder (`tests/fixtures/egg/azo/oracle/README.md`)
and as a compressor on chosen inputs (`README-probes.md`, sets `p2`..`p20`); the textbook binary arithmetic coder
of Witten, Neal and Cleary (1987) and the LZMA SDK's public `DOC/lzma-specification.txt`, both only as hypotheses.

**How the probes read the model.** Most facts below come from sweeps: a fixed prefix, then a byte X that takes
many values, then `z` × 40. The decoder is run up to X; the coder's value relative to its interval, over all X,
draws the cells of X's 256 values, and the cell edges give every probability used for X (`tools/azo/sweepmeas.py`). Ratios of cell edges do not depend on errors earlier in the stream,
so a full 256-value sweep reads a probability tree exactly even when the decoder is slightly off before X.

### 3.1 The arithmetic coder

A binary arithmetic coder with 32-bit `low` and `high` (inclusive), bit-wise renormalisation and underflow
handling, decoding from the block's first byte (no header byte):

```text
init:   low = 0, high = 0xFFFFFFFF, value = first 32 bits of the data (missing bits read as 0)
decode(p, B):              # p: probability of 0 with B bits (B = 12, or 10 for literal bits)
    range = high - low + 1
    bound = (range >> B) * p
    if value - low < bound:  bit = 0; high = low + bound - 1
    else:                    bit = 1; low  = low + bound
    loop:
        if high < 0x80000000:                              pass
        elif low >= 0x80000000:                            low -= 0x80000000; high -= 0x80000000; value -= 0x80000000
        elif low >= 0x40000000 and high < 0xC0000000:      low -= 0x40000000; high -= 0x40000000; value -= 0x40000000
        else: break
        low = 2*low; high = 2*high + 1; value = 2*value + next bit
```

Evidence: every sweep set (`alzip86_p4`..`alzip86_p7`, 2,816 streams) decodes exactly; the exact lower edges of
`abcde` + X measured with ALZip as a decoder (`oracle/edges_abcde_L100_K11.json`) match this coder to within one
unit for 244 of 249 X.

### 3.2 Probabilities

Every decision has an adaptive probability of a 0. After a 0: `p += (T - p) >> s`; after a 1: `p -= p >> s`.

| Decisions | Bits (T) | Start | Shift s |
|---|---|---|---|
| literal bits | 10 (1024) | 512 | 4 |
| distance-slot tree (3.6) | 10 (1024) | 512 | 4 |
| length tree after a new distance (3.6) | 10 | 512 | 4 |
| all other decisions | 12 | 2048 | 6 |

Evidence: the flag along a literal run reads 2050, 2082, 2113, ..., 2573 at positions 1..20 (`alzip86_p15` `C*`:
2048 plus the shift-6 steps); a literal tree trained once reads 544/480 at its node (`p9`, `p10`); the
distance-slot tree at a second new-distance match fits 10-bit probabilities with shift 4 and no other: following the
exact integer coder along the known symbols (`tools/azo/pathcheck.py`) is consistent for all 105 second matches of
`alzip86_p18` and `p21` and their next literal with that tree, and for none with 12-bit trees of shift 4, 5 or 6.
The length tree's format is from `p8` (first matches after 64 literals decode with 10-bit, not 12-bit, trees).

### 3.3 Literals

A literal is 8 decisions, most significant bit first, down a tree (node 1, then `2*node + bit`). The first byte of
a block is a literal with no flag. Let `prev` be the previous output byte (0 for the first byte). There are two
tree sets:

- F: one tree per value of `prev` (256 trees);
- G: one tree per `prev >> 5` (8 trees).

Every literal **updates both** of its trees (F[prev] and G[prev >> 5]) with its bits, but is **decoded with one**
of them, chosen by a counter per `prev` value (start 0):

```text
use G if counter[prev] > 0, else F
after the literal: cF = sum over its 8 bits of log2(P_F(bit) after this bit's update), cG likewise with G
    if cG > cF: counter[prev] = min(counter[prev] + 1, 9)
    if cF > cG: counter[prev] = max(counter[prev] - 1, -9)
```

The costs use each tree's probability of the decoded bit **after** that bit's update (`q + ((1024 - q) >> 4)`
where q is the probability the tree gave the bit). With the probabilities before the update, 2535 of 2546 labelled
choices fit; after the update, all 2546 do, including a choice in real text (`p24`/`p26` `src`, byte 30: the first
use of context `n` favours F by 0.099 bit before the update and G by 0.013 bit after, and ALZip uses G next time;
the tree used at bytes 1..81 of that text was read with `p26` probes and agrees everywhere). The limits ±9 are not
measured.

Evidence: the `p12`/`p13` probes (prefix `x0 x1 c d c`, Y swept) read either F[c] or G[c >> 5] exactly at Y, and
whether G is used follows whether G or F would have coded `d` better (all 448 `p13` and 293 of 294 `p12` streams
decode); forcing either tree alone fails. The counter's limits and the comparison's exact integer form are not
measured; the log2 form with eps 0.003 reproduces all probes (3.8).

Bytes copied by a match do not train any literal tree (`alzip86_p14`, `p19`, `p20`: a literal after a match reads
the trees as trained by literals only; `p20` reads the full tree after a second match exactly as F).

### 3.4 Symbols

```text
symbol (not before the first byte):
  flag (context 3.5)         0: literal (3.3)   1: match
  match:
    bit A (context: for each of the last 8 matches, whether it was a history match)
      A = 1: history match (3.7): bit A1, then a history index (1 bit if A1 = 1, 7 bits if A1 = 0)
      A = 0: bit S (context: for each of the last 8 matches, whether it was a repeat-distance match)
        S = 1: repeat distance: bit S1 picks rep[S1] (rep list starts [1, 2]; the used entry moves to the front);
               length from the repeat-length coder (3.6)
        S = 0: new distance (3.6); the distance goes to the front of rep (the list keeps 2 entries);
               then length from the match-length coder (3.6)
```

Evidence: `p8` (one new-distance match, distance 1..200) for the distance code; `p14`, `p15` `A*` (a repeat match
after a new one) for the separate repeat-length coder; `p16` `c*` (a history match copying 3..16 bytes of the block start after 16 literals:
A1 = 1 for 3, A1 = 0 and L = m - 2 for m = 4..16) and `p17` (the same after a new-distance match: L = m - 1),
read bit by bit from the coder's value; `p15` `B*`, `p17`.

### 3.5 The flag's context

The flag's probability is chosen by the kinds of the last 8 symbols (literal or match): `ctx = sum of
(is_match << age)` over the last 8 symbols. A block that has only had literals uses context 0 throughout.

Evidence: `alzip86_p15` `A*` (after a match, 0..15 literals, then X): the flag reads 2048 (a fresh context) for 0..7
literals after the match and 2436 at 8 = the shared context of the literal run (2476 at position 16 in `C*`, then
lowered by the match's 1 with shift 6: 2476 - 38 = 2438). `p14` shows the same for 0..3. Whether it is exactly
"the last 8 symbols" or "literals since the last match, capped at 8" is not settled (3.8).

### 3.6 Distances and lengths

```text
new distance: slot G (7-bit tree, 10-bit probabilities, shift 4)
    G < 20:  distance = G + 1
    else:    nb = (G - 16) >> 2 extra bits e (each coded with a fixed probability 1/2, not adaptive), j = (G - 16) & 3,
             distance = 16 + 4*(2^nb - 1) + j*2^nb + e + 1
length (match-length coder after a new distance, repeat-length coder after a repeat distance):
    slot G (7-bit tree)
    G < 40:  length = G + 2
    else:    nb = (G - 32) >> 3 extra bits e (fixed probability 1/2), j = (G - 32) & 7,
             length = 32 + 8*(2^nb - 1) + j*2^nb + e + 2
maximum length 32 793 (slot 127, 11 extra bits)
```

The match-length coder after a new distance uses 10-bit probabilities with shift 4, one tree per distance slot G,
and each match updates its tree **twice** along the decoded path. Evidence (`tools/azo/pathcheck.py`, exact integer
paths): a second match with another slot reads a fresh tree (`p18`, `p21`: 105 of 105); in `p22` the third match
(slot 24, after slots 23 and 24) reads its tree exactly as "two updates with the second match's length" and as nothing
else tried (one update at shift 2..5, trees shared with slot 23); the extra bits of distances and lengths fit only
with a fixed probability of 1/2 (`p22`). The double update is the simplest rule that fits; it is not proven. The
repeat-length coder is a separate tree (12-bit, shift 6).

### 3.7 History matches (A = 1)

An A match repeats an earlier match: same length, copied from the earlier match's destination start. The decoder
keeps a **match history**, a list of (destination start, length), most recent first:

```text
at block start: history = [(0, 2), (0, 3), (0, 4), ..., (0, 129)]     # 128 entries: the block start, every length
after a new-distance or repeat-distance match at position p with length L: insert (p, L) at the front, drop the last
A match:  A1 = 1: index = one bit (10-bit tree LA1);  A1 = 0: index = 7-bit tree LA (10-bit probabilities)
          (start, length) = history[index]; copy `length` bytes from `start` (distance = pos - start)
          the used entry moves to the front (`p24` `src` byte 158: index 2 only with this rule)
```

Evidence: `alzip86_p32` (k = 1, 2 earlier matches of length 4, then a copy of the last or the previous match's
4 bytes): ALZip codes these as A with index 0 (last) or 1 (previous), short form; copies of only 2 or 3 of those bytes
are coded as new distances. With no earlier match, copies of the block start of length m read index m - 2 (`p16`);
after k matches they read m - 2 + k (`p17`, `p22`), because k entries were inserted in front of the initial ones.
The three realistic-data positions that an exact-context LZP table could not explain (`p24` `bin_table` byte 17,
`doc` 164, `src` 150) are history indexes 0, 0 and 3. This replaces the earlier "LZP match" reading of A, which only
looked right because every probe copied the block start.

### 3.8 Open points

- **The flag's probability after several matches.** With two or more matches in the window, some literals need a
  flag probability that no context of 3.5 holds (`p22`, `p18` set 1, `p24` `rnd_text` byte 155: needed about
  2400..2600 or 3500 where the model has 2048..2111). The literal trees there are right (full 256-value sweeps
  `p23` read them exactly). This is the main reason realistic data fails.
- **Realistic data** (`p24`: the project's own C source and documents, generated binaries, 300 bytes to 600 KiB)
  now fails after 17 (`bin_table`), 150 (`src`), 164 (`doc`), 280 (`bin_mixed`) and 302 (`rnd_text`) bytes.
  The history reading of A (3.7) moved these to 28, 158, 185, 280 and 302 bytes; there the coder state is already
  slightly off, so some earlier decision still has a wrong probability.
- History matches (3.7): whether repeat-distance matches are inserted (assumed yes).
- The A bit's context: keyed by which of the last matches were history matches (a bit per match). This decodes
  the most realistic data (`p24` `doc` 185 → 208 bytes, `src` 158 → 207; at `src` bytes 158 and 181 the A
  probability must be near 2048 where one shared context would hold 2168). Histories of 3 to 8 matches decode the
  same; 8 is assumed. Keying by the symbol history (as the flag) or by the last match kind does worse. A best fit.
- The match-length coder keyed by distance slot (3.6) is the best fit, not a proof.
- The literal counter's exact integer comparison and limits (3.3).
- The streams of extreme redundancy (`com__02_a_x100000`, `03_ab_x100000`, `09_zeros_64k`: 7 to 12 packed
  bytes) fail after the first maximum-length match.
