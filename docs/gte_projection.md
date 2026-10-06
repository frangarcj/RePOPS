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
