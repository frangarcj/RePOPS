# Signed halfword reads and guarded execution support

POPS +0x1DE8 is the LH helper, not a variant of the byte-store helper +0x1DD0.
The dispatcher now accepts +0x1DE8 and specialized entries +0x1E00 (scratchpad),
+0x1E20 (BIOS) and +0x1E40 (I/O). It reuses the LHU routing while preserving
signed RAM/BIOS/scratchpad values and service width 1, rather than LHU width 5.
Unsupported device handlers still stop. This remains a partial original helper;
the three specialization entries are not counted as new reconstructed functions.

`rp_halfword_value` centralizes the wire-to-register conversion. Focused checks
distinguish 0xFFFF8001 from 0x00008001 and preserve positive 0x7FFF. Existing
guest clock accesses remain named through the clock layout.

The execution adapter also adds RAM LH/LHU fast entries using its existing
guarded-memory pattern. Every invocation checks the address class before
translation. An I/O miss returns to the corresponding C helper with the original
address intact. This is execution support, not a new firmware implementation.
Cache tests exercise both signs and the same callsite switching from RAM to I/O.

The pre-fast integrated run `out/lh-vertical.Z3cQr8/result/` reaches the next
boundary: +0x1DD0 attempts a byte write outside RAM at 0x1F801800 and stops at
+0x1C70. The existing device table maps this port to CD writer +0xD1B0. No CD
command completion or game boot is inferred from reaching the write.

That run records 38,128,915 generated-cache hook observations and 3,203 transfers.
Fast-helper instructions are excluded from the hook counter. The native game
flag remains false. Evidence is the original listing +0x1DE8..+0x1FD0 and the
emitter's LH helper selection in `pops_emit_memory.c`.

## Measured reduction in execution-adapter crossings

`out/half-fast-vertical.HU29bd/comparison.json` compares the pre-fast run with
the guarded version. Helper-exit trace events fall from 129,923 to 75,517:
54,406 fewer (41.88%). Both runs stop at the same helper/guest PC with the same
38,128,915 instruction-hook observations and 3,203 compiled-block transfers.

All 58,245 selected observable events match in order: SPU writes, timer writes,
event deadlines dispatched, active sample returns and the final blocker. This
is an integrated regression check, not all-memory or whole-emulator equivalence.
The fast wrapper run took 52.29 seconds on this machine; no baseline wall time
was recorded, so no wall-time speedup percentage is claimed.
