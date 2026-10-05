# Conditional flow in the native diagnostic

The +0x58C0 record controller now handles ordinary conditional delay slots.
When the slot overwrites a compared source, +0x5064 captures the condition
before emitting the slot; +0x4340 consumes the saved temporary. The controller
retains explicit limits for load hazards, nested branches and constant-condition
folding. The +0x6914 branch category emits forward patches or backward loop
edges with the original T9 countdown check and event handoff.

The first example is BNE T2,T3 at guest BFC00270 followed by ADDI T2,T2,0x80.
The emitted comparison must see T2 before that increment. The second similar
loop is at BFC00320. Both now finish in the generated-code adapter, invoking
the same reconstructed compiler for each next block rather than executing
original firmware instructions.

The original +0x96AC helper is also reconstructed: an enabled pending
interrupt zeroes the remaining countdown and adjusts the deadline. A call
with no enabled pending interrupt returns without fabricating an interrupt.

`out/ffvi_run.gQRrY9/result/` records 702 generated instructions and four
compiled-block transfers. The BIOS reaches BFC0032C; the next compiler limit
is a COP0 write at state offset +0x11C. These are emitted-instruction counts,
not original function counts or a completion percentage. The original PSP
UI is still explicitly bypassed in the diagnostic. FFVI has not booted.

Focused validation: emitter smoke checks the captured register and branch
debit; the existing branch-delay/FPR-bit adapter smoke passes. The integrated
run exercises the two BIOS loops. This is not a full binary-equivalence or
interrupt-timing validation.
