# Timers, exception vectors and the SPU read boundary

## Reconstructed timer paths

The +0x9C60 timer writer now follows its mode, target and counter-origin paths,
rather than accepting only zero resets. Clock selection, gating state and
existing-event removal follow the original disassembly. The counter-write path
resets the origin; it does not use the supplied counter value in this POPS build.

+0x9B6C synchronizes elapsed guest cycles with the counter, tracks target and
wrap flags, and preserves the fractional clock phase through the origin.
+0x9A54 computes the next deadline from the timer period and interrupt policy.
The existing +0x945C event insertion body moved from pops_graphics.c to
pops_events.c so video and timers use the same implementation.

Focused tests cover timer 1 mode 0x100 (shift 11), a changed target and the
resulting earliest-event deadline. The full timer interrupt callback +0x9AD0
is not supplied by these changes; the subsystem is still incomplete.

## RAM exception vector

+0x94C4 now selects the reconstructed RAM compiler for a missing 80000080
vector, instead of always calling the BIOS-only compiler. The integrated run
actually reached this path through a guest syscall with cause 8.

## SPU register reads

+0x85F4 is reconstructed for signed and unsigned byte/halfword reads and word
reads. Every access debits ten guest cycles; words debit seven more. Halfword
reads of SPUSTAT combine the shared state with GP+0x34A and copy status bit 5
to bit 7. Reads do not clear that latch, invent a ready state, or execute the ME.
The focused SPU test checks widths, cycle debits and status-read side effects.
Dynamic memory helpers also route reached SPU writes to the existing +0x7F00
implementation and ordinary shadow-register reads to the reviewed +0x8A54 path.

## Integrated result

`out/ffvi_run.rvvDi7/result/` reaches SPUCNT = C010 after passing timer and
exception-vector setup. The existing cooperative SPU writer then calls the
native ME worker, which requests the POPS sample callback at offset zero.
The run stops at `active_or_busy_SPU_callback_not_reconstructed` because only
its disabled-SPU branch has been reconstructed at this checkpoint.

Observed: 9,580,458 generated-cache instruction hooks, 1,688 entry transfers,
and 211 host service calls. These exclude the execution-adapter thunks and are
not original-function coverage. The run has an explicit PSP UI bypass and
`game_executed: false`; no active mixing, audible output or rendered frame is
claimed. This is a host integration result, not full instruction equivalence.

Source: local hash-pinned POPS disassembly at +0x9A54/+0x9B6C/+0x9C60,
+0x94C4 and +0x85F4, plus the original helper callers. Recovered names are our
descriptions rather than Sony debug symbols.
