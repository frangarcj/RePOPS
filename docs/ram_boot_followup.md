# RAM boot: immediate shifts and interrupt writes

The generic immediate-shift category of +0x6914 now emits SLL/SRL/SRA and
updates the known-value state. Recognized paired-shift optimizations still
stop explicitly rather than silently replacing the original optimized path.

The +0x98C4 interrupt writer now handles I_STAT's AND acknowledgement,
I_MASK replacement, COP0 cause changes and bringing a pending interrupt's
deadline forward. Its original implementation ignores the width parameter.

`out/ffvi_run.LDgfH0/result/` runs RAM and BIOS initialization to the next
missing helper, the unsigned byte reader +0x2468. It records 169,448 generated
instruction-hook observations and 94 block-entry transfers. The last saved
guest block PC is BFC02B50. FFVI has not booted and there is no renderer or
active SPU voice output yet.

The existing focused emitter and event tests cover the added shift result
and interrupt acknowledgement/mask/deadline effects. These are contract tests
plus a host run, not full binary equivalence.
