<!-- Sources: inherited independent docs/specs/azo.md and revision 3e548f80; own chosen-input ALZip outputs and E01-E24 records. No external AZO implementation. -->
# AZO decoding investigation: 2026-10-02 checkpoint

This is a reproducible partial model, **not a complete decoding specification**.
The experiment notebook is [protocol.md](../cleanroom/azo-2026-10-02/protocol.md);
provenance and its limits are in [the evidence guide](../cleanroom/azo-2026-10-02/README.md).
The inherited specification records earlier sessions' claims. This document
separates those claims from this continuation's measurements.

## 1. What was actually observed

The compressor was used through its ordinary command line. We chose plaintext,
retained the EGG output, extracted the method-3 payload with the existing
clean-room container parser, and tried independently written hypotheses against
that payload. Expected plaintext was used only after decoding to compare bytes.
No vendor executable was opened, hashed, disassembled, debugged, traced, or read
as source. Traces below come from **our model**, not the compressor.

The input suffix `.com` requests an encoder behavior observed in earlier probes;
these inputs are data and were never executed. Reading binary archive output is
the black-box measurement, distinct from inspecting the executable implementing
compression. No external AZO implementation or web source was consulted here.

The baseline is the committed model at `3e548f80`, with full-output overrun
checking added by the audit harness. The pre-existing uncommitted `azo3.py` and
p35 corpus were preserved and excluded. The dated directory contains exact
source snapshots for the final experiments, per-file SHA-256 and CRC checks,
failed hypotheses, command exit codes, and deterministic input recipes.

## 2. How a candidate decoder works

This section explains the independent model we tested. It does not assert that
the compressor uses these internal data structures, or that every rule is final.

### Container and block

The inherited EGG reader supplies method-3 packed bytes. Observed AZO streams
start with `31 00`. Each block has three big-endian 32-bit words: uncompressed
size U, packed size C, and U XOR C. Twelve zero bytes end the stream. U equal to C
means stored bytes in this model; otherwise C bytes enter the arithmetic decoder.
The inherited container specification describes the outer CRC. The harness
records both archive-relative and payload-relative block offsets.

An empty EGG has no data block and no block CRC. It is classified separately.
Copying stored bytes is also not evidence of successful arithmetic decoding.
A compressed-block success requires exact full output, correct length, no error,
and agreement with the outer CRC. A prefix match is diagnostic only.

### Arithmetic decisions and adaptive probability

The coder maintains an inclusive 32-bit interval [low, high] and a coded value.
For a binary decision with probability integer p and precision B, split at
`low + (((high - low + 1) >> B) * p)`. The coded value selects a side; the coder
then renormalizes the interval and consumes bits using the inherited E1/E2/E3
rules. This integer operation is not interchangeable with floating point.

A probability starts at half its scale T = 2^B. After decoding bit 0, update
`p += (T - p) >> shift`; after bit 1, update `p -= p >> shift`. A context chooses
which p is used. The same arithmetic rule with the wrong context can reproduce
many early bytes and still diverge later.

A bit tree stores a separate probability at each node. Start at node 1; after
bit b, move to `2*node + b`. Seven decisions identify one of 128 slot values.
Literal trees use eight decisions. Extra bits for large slots are modeled as
fixed-half decisions. E14 did not distinguish quantized-half and exact-half
variants by full-pass count; it did not prove them interchangeable.

### Literal or match, then match kind

The inherited candidate reads a literal/match flag using the last eight symbol
kinds (literal 0, match 1), padded initially with zeros. A literal uses one of two
probability trees: F indexed by the previous byte, G by that byte shifted right
five bits. Both trees learn the decoded literal. A per-previous-byte counter
selects a tree using the post-update bit costs. The exact selector remains open;
realistic p24 inputs still fail. This is a major obstacle to a complete decoder.

For a match, A selects a saved match-history entry. If A is false, S selects
repeat distance R versus newly coded distance D. The inherited A/S contexts use
recent match kinds; literals do not advance those histories. The new A1 candidate
uses recent outcomes of A1 itself, advanced only when A1 is read. Eight bits are
a tested setting, not an established minimum or unique width.

The history starts with (source position 0, length 2..129). A uses either a
seven-bit index or a short index selected by A1. It moves the selected entry to
the front. D/R insert their destination and length. Repeat distances start with
1 and 2 and move to the front when selected. These remain model rules, not
observations of vendor memory. The old LZP explanation has been superseded by
the earlier match-history experiments; leftover LZP bookkeeping in the research
script is not evidence that the format requires LZP.

### Distance and length slots

For a distance slot g below 20, d = g + 1. Otherwise:

```
nb = (g - 16) >> 2
j  = (g - 16) & 3
d  = 16 + 4 * (2^nb - 1) + j * 2^nb + extra(nb bits) + 1
```

For a length slot g below 40, length = g + 2. Otherwise:

```
nb = (g - 32) >> 3
j  = (g - 32) & 7
length = 32 + 8 * (2^nb - 1) + j * 2^nb + extra(nb bits) + 2
```

The largest seven-bit length slot permits 32793. audit36 includes lengths on both
sides of that boundary and longer streams requiring multiple matches. This work
rejected proposed special meanings for slot 127: one constant-byte input could
appear to fit a sentinel while other periods failed.

Copy one byte at a time from output[-d]; overlapping copies are allowed. Check
1 <= d <= current position and reject a match extending beyond the block. The
old model's output clipping could otherwise hide a wrong overlong match.

### The new, provisional repeat-length rule

E20 uses a repeat-length tree associated with distance d. It receives one update
along the decoded length-slot path after a new-distance D match. R decodes its
length through that same tree, which learns once as it is read. This is in
addition to the inherited D-length model, which remains keyed by distance slot.
The repeat selector S1 uses 10-bit probabilities and update shift 4 in this
candidate. The inherited baseline uses 12-bit/shift-6 S1.

This rule is **supported as a candidate, not uniquely identified**. E21's
actual-distance key, distance-slot key and reset-on-new-D variants tie on the
selected corpus. We need different distances in the same slot and deliberate
cache reuse/eviction to distinguish them. A shared global tree was decisively
rejected by the old control probes. An arbitrary length threshold is not adopted.

## 3. Why these new inputs were chosen

### audit36: 105 boundary and repetition probes

Five periods (zero, `a`, `ab`, `abc`, and 37 distinct bytes) cross 21 lengths:
40, 41, 42, 1000, 4096, 8192, 16384, 32760, 32767, 32768, 32769, 32792, 32793,
32794, 32800, 40000, 65535, 65536, 65537, 100000, 131072.

Lengths around 40 distinguish direct and extra-bit length slots. Lengths around
32793 distinguish a maximum individual match from a proposed escape. Larger
lengths force subsequent decisions. A constant byte can conceal an incorrect
copy distance, so periods 2, 3 and 37 are essential controls. The manifest gives
the exact repeated bytes and length of every input.

The baseline passed 75/105. A1-history plus S1-precision changes passed 91/105.
Adding one global D-to-repeat-length update passed 101/105 but destroyed many
older cases. Therefore those 101 successes alone did not justify that rule.

### audit37: 66 probes of length-model training

Start with byte aa and bytes 21..45 hex, then copy distance 37 for L bytes. The
extra leading byte makes the first intended source start at position 1, avoiding
the history's seeded source 0. Insert fa fb fc, copy distance 37 for K bytes,
then append fd fe and forty `z` bytes. L sweeps 22 lengths across slot boundaries
and K is 5, 43, or 257. These are intended matches, not assumed compressor tokens.

A separator prevents an intended first copy from extending into the second. The
second copy asks whether the first length trained a model used at that distance.
The final different-distance tail tests whether training improperly contaminates
another distance. This is why full-file checks matter more than a longer prefix.

Without the new training, the baseline passes 3/66. Unconditional global training
passes 0/66, although it improves the middle prefix. Distance-associated training
passes 61/66 while preserving the older controls. That contrast motivated E19.

## 4. Worked decoding observations

See [worked-traces.json](../cleanroom/azo-2026-10-02/worked-traces.json) for complete
own-model token traces, length slots, input/archive hashes and block offsets.
Output positions below are zero based. Arithmetic bit positions there belong to
our reader and are not vendor process observations.

| Input | Own-model matches (position: kind, distance, length) | Result |
|---|---|---|
| audit37/l00008_k005 | 38: D,37,8; 49: R,37,5; 57: R,1,39 | All 96 bytes and CRC agree |
| audit37/l00043_k043 | 38: D,37,43; 84: R,37,43; 130: A,92,43 | First mismatch at 129; later A exceeds block; rejected |
| audit36/abc_100000 | 3: D,3,32793; 32796: A,32793,32793; 65589: A,65586,32793; 98382: R,3,1618 | All 100000 bytes and CRC agree |
| audit36/ab_100000 | 2: R,2,32793; 32795: R,2,32793; 65588: A,65586,32793; 98381: R,2,1246 | First mismatch at 99627; later block overrun; rejected |

The last row requires 1619 remaining bytes at position 98381, but the model reads
1246. That localizes a failure; it does not prove that the compressor emitted an
R token of length 1619. Once our arithmetic state is wrong, later token labels
can be meaningless. Likewise, a failed tail in audit37 does not by itself prove
that the tail length tree is the cause; an earlier probability error can surface
at a later literal or flag.

## 5. Broad validation and rejected alternatives

E20 evaluates com, p2..p34 and the two new batches, totaling 17963 archive/input
pairs. These include correlated sweeps, not 17963 independent kinds of real data.

| Classification | Baseline full matches | E20 candidate full matches | Total |
|---|---:|---:|---:|
| Compressed AZO | 15097 | 15189 | 17918 |
| Stored AZO | 41 | 41 | 41 |
| Stored EGG | 2 | 2 | 2 |
| Empty EGG, no CRC | 2 | 2 | 2 |
| All pairs | 15142 | 15234 | 17963 |

There are 92 new successes and zero baseline-success regressions. There are still
2729 failures. In particular p24 is 0/14. The candidate is a useful research
checkpoint, not a generally usable decoder. E15 used an earlier comparator that
counted two empty files as failures; those historical logs are retained unchanged.

The global-training candidate E15 passed only 10782/17897 versus baseline
15137/17897. p14 alone fell from 2048 successes to zero. E16 isolated the regression
to global length training rather than the A1/S1 changes. E22's second update on
R lengths regressed audit36 from 101 to 85; training A lengths with their effective
distance did not improve the tested full-pass count. These are rejected changes,
not omitted inconveniences. The full run inventory retains other unsuccessful
context, precision, sentinel and arithmetic-half hypotheses.

The frozen E20 result reproduces all 17963 outcomes, including failures. Evidence
tests separately regenerate all 243 new plaintexts, verify capture hashes and
successful command exits, and test that wrong plaintext, partial output and
block overruns cannot count as full successes. These do not constitute malformed
input safety testing. The inherited arithmetic coder still pads exhausted input
with zeros and must not be used as a production decoder.

## 6. Later challenge: audit38 and the limits of our conclusion

After freezing E20, E23 generated 72 new inputs with two different copy distances
before revisiting the first. Distances 37/38 and 37/40 share a slot; 37/41 cross a
slot boundary; 8/37 is a separate control. A deterministic 160-byte xorshift32
prefix, separators and chosen lengths make every input reproducible. The exact
recipe and seed are retained, as are all ordinary compressor commands.

Baseline passes 0/72; E20's candidate passes 29/72. Actual-distance, slot-keyed and
reset variants each pass 29. One failed case has prefix 210 versus 215; both fail,
so that difference cannot establish the correct key. Own-model traces show some
cases decode the second intended D length incorrectly, before the repeat that
was supposed to distinguish the hypotheses. E24 therefore tests D-length context
and update alternatives; all three alternatives pass 4907/4959 of its selected
controls. That is a failed discriminator, not evidence of equivalence on all data.

Combining disjoint E20 and E23 corpora gives 18035 pairs: baseline 15142 matches,
candidate 15263, 121 gains, zero lost baseline successes, and 2772 remaining
failures. The 45 empty/stored cases are included in each total and do not exercise
compressed AZO. This broader result supports further investigation of the
candidate but does not resolve the outstanding context questions.

## 7. Next discriminating experiments

1. Use two distinct distances in the same distance slot, separated by different
   chosen lengths. Then reuse each distance. Compare exact-distance, slot-keyed,
   and movable/reset cache hypotheses. Record intended versus observed own-model
   parses; avoid assuming the compressor chose our intended matches.
2. Extend audit37 with controlled final literal sweeps around the five failing
   tails. Determine whether the first wrong decision precedes the tail match.
3. Isolate repeated maximum-slot decisions with nonconstant periods. Distinguish
   A1 context/history advancement from repeat-length updates without forcing bits.
4. Resolve literal selector and later flag contexts on p24/p23/p30/p33 before
   promoting the model into a C decoder. Exact full bytes and CRC must pass.
5. Specify termination, truncation and invalid-distance handling independently
   before production integration. Reproducibility is not decoder completeness.

This checkpoint changes the research specification and adds experimental tools;
it does not silently replace the pre-existing working decoder or claim that the
clean-room goal is finished. Hashes, local logs and commits support auditability,
but are not external timestamping or a legal guarantee about historical conduct.
