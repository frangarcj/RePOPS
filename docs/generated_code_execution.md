# Provisional generated-code execution adapter

The reconstructed POPS compiler emits Allegrex, not callable ARM64. The host
now has a deliberately small execution adapter for that generated cache.
This is support for the POPS reverse, not a replacement emulator or an
optimized translator. `generated_code.c` is new support code and must not be
counted as a recovered original function.

Only instructions in the C-produced BIOS code cache are fetched. Original
POPS helper targets are dispatched to C reconstructions or stop explicitly;
there is no fallback interpreter for the original PRX. The adapter implements
the integer/FPR-bit operations needed by the first block and preserves branch
delay slots. Unsupported words retain their address and encoding in the trace.

`pops_dispatch.c` reconstructs the entry/cache lookup, the default write helper
at +0x89A0 and the RAM cache invalidation at +0x7E60. It begins the link helper
at +0x2888 and calls the existing C compiler on a miss. Cache barriers use
host-coherent memory; no PSP instruction-cache behavior is claimed.

`REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh` recorded
`out/ffvi_run.NFCRTG/result/`: 86 generated instructions executed, including
the BIOS memory-control writes and cache-control helper. The resulting PS1
target is **0xBFC00240**, and compiling that next block stops at the
unreconstructed complex delay-slot controller +0x61B0.

This is execution of the reconstructed compiler's output for BIOS code, not
gameplay and not evidence of complete PS1 semantics. `game_executed` remains
false. Reports now explicitly distinguish native reconstructed POPS C from
the generated-code adapter instead of claiming there is no interpreter at all.

`make test-generated-code` supplies one small synthetic check for delay-slot
stores, branch/call flow, signed extension and FPR bit transfers. Full timing,
VFPU behavior, exceptions and the unimplemented opcode set remain out of scope.
