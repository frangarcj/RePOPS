# BIOS reaches its RAM-resident program

The next +0x58C0 controller path no longer rejects every load in a branch
delay slot. +0x6164 inspects the target instruction and only selects the
deferred-load path for the original hazard patterns. Those deferred patterns
and nested branch slots still stop explicitly.

The BIOS byte-copy helper +0x1DD0 now writes RAM, and +0x2128 reads words from
RAM or BIOS with the original scratchpad/ROM cost adjustments. Its initial
ROM read changes the call site to +0x2160; Unicorn exits there to the reviewed
C rather than executing original PRX bytes. Device I/O remains incomplete.

The emitter now handles state-read records and COP0 return-from-exception
records through the existing load/state and interrupt-deadline machinery.
Focused emission assertions extend `make test-native-emit`.

`out/ffvi_run.AaJVia/result/` reaches the BFC00420 ROM-to-RAM copy and then
requests execution at A0000500. The observed helper arguments progress from
the BIOS source to its final word; the next explicit stop is
`non_BIOS_compiler_controller_not_reconstructed` at +0x58C0. This is a new
execution region, not a completed PS1 boot. The run records 146,695 generated
instruction-hook observations and 34 block-entry transfers, with
`game_executed: false`.

No RAM block is compiled or executed by this increment. The separate RAM
compiler prologue, cache publication and execution-cache support are the next
workstream. A more detailed post-run copy audit was not executed; the result
above is the integrated execution trace, not a byte-for-byte copy proof.
