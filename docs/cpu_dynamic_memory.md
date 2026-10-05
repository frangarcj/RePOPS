# Dynamic memory emission reaches BIOS RAM clearing

The +0x3A90 emitter now reconstructs the generic dynamic-base path: argument
placement, displacement folding, register flushing and selection of POPS's
original read/write helpers. The stack/cached-base specialization and special
unaligned combinations remain explicit boundaries. The generated code is not
replaced by a different PS1 interpreter or by a synthetic host instruction.

The +0x2450 word-store helper has its RAM path in C. It classifies the address,
maps its low 21 bits into the 0x09800000 RAM backing and stores A1 before
returning. Non-RAM and unaligned cases still stop. Its PC is registered as a
Unicorn exit, so the original helper instructions are never executed.

The +0x6914 ALU category now retains the register allocator, constant-state
propagation and FPR-bit move paths. External direct jumps use the existing
cost/debit and target-link machinery. The first dynamic example is the BIOS
SW/add/SLTU loop at BFC003B8, clearing RAM through a changing pointer.

`out/ffvi_run.arOBWS/result/` publishes a 272-byte block beginning at BFC0039C
and executes RAM clearing until the emitted countdown check reaches +0x1A68.
The diagnostic records 24,475 instruction-hook observations and six compiled
entry transfers. This is a scheduler handoff boundary, not a completed BIOS
boot. The next reverse target is +0x1A68 and the +0x953C event controller.

Focused emitter checks cover a dynamic SW's arguments/delay slot and SLTU
register allocation. Existing emitter and Unicorn reentry checks remain in
place. These are smoke checks and an integrated execution trace, not a claim
of equivalence for all memory classes or ALU inputs. FFVI has not booted.
