# COP0 state writes after the first BIOS loops

The reconstructed +0x46A0 emitter now covers ordinary word/halfword state
stores and the original FPR-bit store shortcut. Cause writes preserve the
other bits in the pending-interrupt byte: emitted EXT/INS merge the software
interrupt bits rather than replacing the entire Cause register. The generated
cache adapter implements those operations, plus LWC1/SWC1 bit transfers.

The +0x6914 COP0 register-3 special case is conditional, not an unconditional
blocker: it only takes the configuration-derived value when the original
source-knownness, preceding opcode and header checks hold. Ordinary zero
writes now take the normal state-store path.

The integrated diagnostic in `out/ffvi_run.Rus1UO/result/` executed 731
generated instructions through five cache-entry transfers and reached BIOS
PC BFC0039C. Compiling the following region now stops at +0x3A90's dynamic
memory-base path. This is the next substantive reverse target, not a silent
fallback to executing the original firmware or treating a memory access as zero.

Focused checks cover zero state writes, the Cause emitted words, and an
EXT/INS bit merge. These do not establish full COP0, exception or timing
equivalence. The C emulator continues to use the provisional interpreter
only for its generated code; no ARM64 block backend or FFVI boot is claimed.
