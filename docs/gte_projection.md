# GTE projection helpers

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
