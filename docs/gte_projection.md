# GTE projection and clipping helpers

The pending RTPT increment is retained in `pops_gte.c` and `pops_gte.h`.
The original entries +0x10B14 and +0x10B24 select the flag-producing and
flag-preserving paths of the shared three-vector projection loops.
The implementation uses named vector, matrix, IR/MAC and FIFO fields.
The reciprocal lookup and low-MAC-word truncation follow POPS rather than
substituting a generic PS1 projection formula. S330 holds raw flag bits.

The emitter uses the original +0x6FF0..+0x70EC flag-use analysis to select the
entry, flushes the original temporary-register range and emits the helper JAL.
Unicorn exits before fetching either PRX entry and dispatches to native C.

`make test-native-gte test-native-emit test-unicorn-cache` passes. Fixtures
cover identity projection, FIFO halfword writes, preservation of the screen
alias and no-flags path, clipping, a reciprocal-table entry and MAC truncation.
These are focused contracts, not complete instruction-level equivalence.

`out/gte-rtpt.eYMNNx/result/` still stops during compilation of the same block:
the following GTE command is NCLIP (6). It records 39,470,213 generated-cache
observations and 40,357 transfers. RTPT has not yet run in that integrated
trace; successful emission must not be reported as executed projection.
Both helper entries remain partial in the reviewed ledger until their full
behavioral scope is checked. The game has not booted and no frame is rendered.

## NCLIP

The original +0x10B34..+0x10B74 routine computes the signed determinant from
the three screen-FIFO vertices, keeps its 64-bit accumulator in HI/LO, writes
the low word to MAC0 and clears S330. The screen FIFO and memory flag shadow
are not overwritten. The native bridge retains the helper's scratch-register
outputs and leaves the higher live temporaries alone.

At +0x10B60 the raw word `0x00C8002E` encodes source registers A2 and T0.
The historical listing's printed zero operand is incorrect; the implementation
follows the word rather than copying that text error. This POPS helper clears
its flags even for a determinant outside signed 32-bit MAC0, so no additional
PS1 overflow flags are invented.

The emitter reproduces +0x71E8: flush through slot zero, then the original
helper call. Focused tests cover both winding orders, degeneracy, full-range
halfword coordinates, signed HI/LO, FIFO/flag-shadow preservation and keeping
slot one live. These do not establish hardware or full-emulator equivalence.

`out/gte-nclip.FWFdKx/result/` now passes NCLIP emission and reaches AVSZ3
(command 0x2D) in the same not-yet-published block. Executed-code counts remain
39,470,213 observations and 40,357 transfers; neither new helper has been
observed on the integrated execution path yet.

## AVSZ3 and AVSZ4

Entries +0x10BF0 and +0x10C38 sum unsigned depth halfwords, multiply by their
signed scale register and retain the full product in host HI/LO. POPS writes
MAC0 from the low word and shifts that truncated signed word by 12 before
clamping OTZ to 0..65535. OTZ is a halfword write; its padding is preserved.
S330 receives bit 18 when the unclamped value is outside that range.

Their emitters (+0x7154/+0x716C) flush dirty HI/LO and invalidate its cached
state without discarding guest GPR temporary slots. The corresponding state
fields are now named in `rp_core_emit_layout`.

Seven native vectors exercise three/four depths, negative scale, saturation,
zero and low-word wrap, including the four-depth product whose signed low
word becomes negative. Emitter checks verify both original JAL destinations,
HI/LO invalidation and preservation of the higher register slots.

`out/gte-avsz.qi8QQM/result/` publishes the block and executes the native RTPT
with-flags helper once. It reaches 39,470,240 observations and 40,358 transfers,
then stops with Unicorn exception 21 on resuming generated code. No NCLIP or
AVSZ invocation is observed in that run. The execution adapter still needs
the emitted VFPU scalar-transfer path; a standalone MFV-to-S330 probe produces
the same exception and confirms that stopping before the instruction avoids
the exception. This is not yet proof of the integrated failing word.

The subsequent run `out/gte-s330.XYmeKl/result/` uses the scalar-transfer
bridge and executes RTPT, NCLIP and AVSZ3. Their observed results and the
configuration checks are recorded in `unicorn_s330_bridge.md`. AVSZ4 and the
no-flags RTPT entry have only focused fixture coverage so far.
