# Reuse generated-code translations between native helpers

This is an execution-harness optimization, not another reconstructed POPS
function or an ARM backend. The native call bridge previously invalidated
both entire Unicorn translation caches before every reentry, even when the
helper only read device state. It now invalidates them when the generated-code
revision changes.

## What marks a revision

- Obtaining a native memory pointer overlapping either generated cache marks
  it conservatively dirty. This includes read access, because the existing
  accessor returns a mutable pointer and several writers use it directly.
- Cached and uncached aliases are normalized before the overlap check.
- Narrow Unicorn write hooks also mark writes by generated instructions into
  those caches and aliases. Ordinary game RAM is not under these hooks.
- First entry always synchronizes. `REPOPS_UNICORN_ALWAYS_FLUSH=1` restores
  the previous every-entry policy for comparisons.

The runtime must reacquire pointers through its accessor for subsequent native
mutations; keeping an old mutable pointer across an execution call and writing
through it afterwards would bypass this bookkeeping. Current native writers
are synchronous accessor users. The revision is deliberately conservative,
not an exhaustive byte-write tracker or hardware instruction-cache model.

The generated-code entry and per-instruction published-range checks remain.
Original PRX memory stays nonexecutable. GPR/FPR/HI/LO transfer and execution
budgets are unchanged. Recovered code-cursor fields now have named native
layout accessors rather than fresh GP offset literals.

## Allocation rewind is not destruction of in-flight code

The configured-PC hook exposed a separate lifetime error in the adapter.
POPS +0x7E60 clears lookup tags and rewinds its RAM allocation cursor, but
does not erase the emitted bytes or rewrite the saved continuation. The
scheduler can still return that continuation through +0x1A68.

`out/scratchpad-warm.z3s4pqd1/result/` reached this case at 0x09542B38;
the adapter incorrectly labelled it an unreconstructed native helper after
the allocation cursor had moved below it. Execution-range checks now retain
the known emitted prefix separately from the current cursor. They do not
enable the entire reserved cache or any original PRX page. Reused addresses
still execute their current bytes through the revision invalidation above.

The focused Unicorn test covers returning to an old RAM-cache block after
rewind, rejecting the unpublished suffix, and observing a later overwrite.
This changes host execution bookkeeping, not POPS's tags, cursor or cycles.

## Checks and measurements

`make test-unicorn-cache` and the same executable with
`REPOPS_UNICORN_ALWAYS_FLUSH=1` both pass helper exits, branch delay slots,
FPR bits, native code patching, alias-based patching, generated-code writes,
RAM/BIOS cache crossing and guarded RAM-to-I/O fallback checks.

An optional `REPOPS_BENCH_REENTRIES=2000` fixture repeats the same generated
block with checked results. In `out/cache-reentry.jajlepih/benchmark.json`:

| Policy | Wall seconds | Translation-cache invalidations in the loop |
| --- | ---: | ---: |
| Every entry | 0.631123 | 2000 |
| Revision based | 0.552797 | 0 |

That single small benchmark is about 12.4% less elapsed time (1.14x speedup),
not a measured whole-emulator speedup. Host load and process startup matter.
No claimed multiplier is inferred from a timed-out integration attempt.

The integrated run `out/cd-reuse.5qcfhr89/result/` completed in 56.504386 seconds
including its wrapper, delivered the first real CD sector and passed the
memory-control shadow write. It stops at the next DMA channel-control write,
PS1 address 0x1F8010B8. It records 38,445,592 generated-code hook observations,
13,404 entry transfers and `game_executed: false`.

The full event prefix through first sector delivery was compared with the
previous successful run `out/cd-sector.fwAsEX/result/`. Both have 477,783
records. Exactly one record differs: `system_time_low_monotonic` contains the
host's different clock reading. All other recorded events match exactly.
The retained difference report is `out/cache-prefix-diff.fdup74m4/comparison.json`.
This is observed-path regression evidence, not universal semantic equivalence.
