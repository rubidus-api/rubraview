<!-- Sources: own handed-over AZO hypotheses and E40 onward chosen-input output experiments. -->
# Reconstructed decoding model: validated research reference

This document describes our model, not observed vendor implementation internals.
The final validation report must be read with it. The reference passes all
18,809 retained pairs, including six inputs compressed after its source was
frozen. Counter saturation and terminal bit handling retain the limits below. No
executable, external implementation, or external format reference was consulted.

## Container and stream

Use the independently handed-over EGG container parser to obtain method 3 data.
The AZO payload begins with bytes 31 00. Blocks have three big-endian 32-bit integers:
uncompressed size U, compressed size C, and U XOR C. Twelve zero bytes terminate
the stream. U==C denotes a stored block; otherwise initialize all coding state
for a compressed block and produce exactly U bytes. Compare archive CRC and the
entire original input independently in research tests. Stored/empty cases do not
establish any compressed coding rule.

## Binary arithmetic state

Maintain inclusive 32-bit low/high, initially 0 / 0xffffffff, and a coded value
initialized from 32 input bits, most significant bit first. For an adaptive bit
with probability p out of2^B, let R=high-low+1 and Z=(R>>B)*p. Value-low<Z selects
zero and high=low+Z-1; otherwise select one and low+=Z. After each selection:

1. If high<0x80000000, shift without subtracting.
2. Else if low>=0x80000000, subtract 0x80000000 from low/high/value.
3. Else if low>=0x40000000 and high<0xc0000000, subtract 0x40000000.
4. Else stop normalizing.

For each normalization, low<<=1, high=(high<<1)|1, value=(value<<1)|next_bit.
An adaptive model starts at half probability. After a zero, p+=(2^B-p)>>s;
after a one, p-=p>>s. Metadata decisions use B=12,s=6 unless stated otherwise;
tree bits use B=10,s=4. A bit tree starts at node 1, reads a bit using that node,
and moves to 2*node+bit. An n-bit tree returns final_node-2^n.

### Uniform multi-bit fields (E52)

Distance and length extra fields are a single uniform subdivision, not merely
n adaptive bits with p=1/2 and not n individually normalized half operations.
For n bits, W=(high-low+1)>>n. Read e=(value-low)//W, bounded to0..2^n-1;
low+=e*W; high=low+W-1; then normalize using the above rules. The candidate
that leaves the final bucket's remainder differs on realistic data; see E52.
This rule and corrected length modeling must be tested together: the inherited
length double-update model concealed the uniform-field correction.

## Literal and length model selection

A literal has an 8-bit tree F indexed by the previous output byte (0 initially)
and a group tree G indexed by previous_byte>>5. Both trees update once for each
decoded literal; copied bytes do not train them. Select G if the counter for
the local context is positive, otherwise F. Counters begin at 0.

A length slot has two 7-bit trees: local F indexed by distance slot, group G by
distance_slot>>4. The same length trees and selectors serve new and repeated
distances (E50/E54). Both update once when a length is coded. History matches
have an implicit length and do not run this length coder in the current model.

For either selector, estimate each tree's probability of the observed symbol
using its post-update bit probabilities q. E68/E69 support the following integer
cost comparison: start score=2^30 and for each q do score=(score>>10)*q. Compare
the two final scores. A larger G score increments the local selector counter;
a larger F score decrements it; a tie leaves it unchanged. The reference uses unbounded signed counters. Clamps of 9 and 31 were
falsified by longer inputs; clamps of 127 and 4096 are observationally equivalent
on the tested validation cohort (E59/E61/E64/E71/E72). A vendor saturation bound,
if any, has not been uniquely identified. Floating sums of logs and exact probability products do not produce
all the same choices as the quantized score. E66 isolates this using prefixes
with no matches, eliminating length/history explanations for those cases.

The earlier apparent double length update was a compensating hypothesis, not
a format rule: the selected group can already have seen several other lengths.
E43/E48 independently vary ordinal, distance slot, and length to expose this.

## Token decisions and sparse histories

The first token is a literal. Before subsequent tokens, a 12-bit adaptive flag
uses the last 8 token kinds (literal 0, match 1), zero padded at block start.
A zero selects a literal. A one enters the match decisions:

- A: context is the last 8 A outcomes across matches. One selects a history match.
- If A is zero, read S: context is the last 8 S outcomes, advanced only when S is
  actually read. A matches do not insert a zero into this history (E63).
- S one selects a repeated distance. S zero selects a newly coded distance.

A repeated distance uses a one-bit tree with 10-bit probabilities tree to select one of two
cached distances, initialized [1,2]; move the chosen entry to front. A new
distance pushes its value at the front and drops the third entry. Both cases
then decode a length from the shared dual length model described above.

## Slot mappings

A new distance slot g is a 7-bit tree with 10-bit probabilities, shift 4.
For g<20, distance=g+1. Otherwise n=(g-16)>>2, j=(g-16)&3, read n uniform extra
bits e, and distance=16+4*(2^n-1)+j*2^n+e+1. A repeated distance supplies the
same slot by inverting this mapping before selecting its length context.

For a length slot g<40, length=g+2. Otherwise n=(g-32)>>3, j=(g-32)&7, read n
uniform extra bits e, and length=32+8*(2^n-1)+j*2^n+e+2. The mapping's maximum
is 32793. Reject a distance outside1..current_output_size or a length exceeding
the declared remaining output, rather than silently clipping it.

## History matches and cached indices (E58)

Initialize 128 history entries (source 0, length 2..129). After a new-distance or
repeat-distance match at output position p with length L, insert(p,L) at front
and drop the oldest entry. A history match copies an existing entry's length
from its retained destination source, then moves that entry to the front.
Copying is bytewise and supports overlap.

A1 chooses long or short index form. Its12-bit probability context is the last 8
A1 outcomes, advanced only when A1 is read. A1 zero reads a 7-bit10-bit-probability
index tree. A1 one reads a one-bit tree with 10-bit probabilities selector into two cached
history indices, initialized [0,1]. A long index enters this cache; a short use
moves the selected cached index to the front. The short bit is NOT necessarily
history entry 0/1: e.g. a preceding long index 6 can make short 0 refer to entry 6.
The history list order and the index cache are separate state.

## Implementation limits

The compact reference permits at most 64 implicit zero bits after the packed
block ends. A proposed 31-bit limit rejected 1,293 normal archives; measurement
found a maximum of 43 bits in the retained corpus (E74/E75). The 64-bit budget
is an implementation bound, not a uniquely reconstructed encoder-finalization
rule. The reference checks arithmetic containment, copy extents, block framing,
and an output-size budget. Its caller must verify the EGG CRC.

Successful full equality and CRC on all retained normal archives do not establish
behavior for every AZO profile or constitute a production security review. No
production C integration is claimed here. The exact encoder saturation policy
and termination construction remain open; the documented research decoder has
no remaining failures in the tested corpus. The evidence does not establish
legal admissibility or retroactively certify other sessions.
