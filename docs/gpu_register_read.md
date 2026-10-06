# GPU status reader: +0x12FBC

The GPU I/O table points to +0x12FBC even though the historical Ghidra
inventory does not list it as a separate function start. This pass
recovers the GPUSTAT branch through +0x1309C; GPUREAD still stops at +0x130BC
after its original cycle debit. No rendered frame or VRAM transfer is claimed.

`src/native/pops_gpu.h` names the state, polling timestamps, frame origin,
display phase and draw-mode fields. `pops_gpu.c` uses these names in native C.
The raw status word stays unchanged: the returned word inserts mode/phase
bits from the current state. Reading status debits one guest cycle and may
debit the current scanline phase for the original rapid-poll condition.
The two polling timestamps retain the time before that additional debit.

The interlaced branch uses the original phase inversion and compatibility
bit 27. The noninterlaced branch computes the line bit from the adjusted
guest time. This is not a synthetic ready response or a wall-clock frame tick.

The direct generated call and the dynamic I/O-table path both enter this C
reader. Only generated cache pages execute under Unicorn; +0x12FBC is an exit
back to native C. The original access-width input is unused by this reader.

`make native test-native-gpu test-unicorn-cache` passes. The focused GPU
fixture covers returned bits, unchanged stored status, timestamp rotation,
rapid polling, exhausted downcount, interlace compatibility and the explicit
GPUREAD boundary. These are contract checks, not exhaustive binary equivalence.

Integrated result: `out/gpu-status.gm0dKa/result/run.json`. Two GPUSTAT reads
return 0x1C800000 from initialized state. Execution continues to the GP0 writer
at +0x127D8 (port 0x1810). There are 39,246,356 generated-cache observations
and 38,998 entry transfers. `game_executed` remains false. A successful status
read is not proof of the renderer, command processor or GPUREAD transfers.
