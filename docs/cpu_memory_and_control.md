# Memory and control-register continuation

This increment continues beyond the RAM execution checkpoint at 901e130.

## Reconstructed paths

- +0x2468: unsigned-byte reads used by BIOS/RAM routines and decompression.
- +0x267C: unsigned-halfword reads, including the existing IRQ reader.
- +0x2110 and +0x2450: RAM stores and dispatch to the reconstructed IRQ,
  timer and DMA-control writers. Other devices still stop explicitly.
- +0x9850: interrupt-register reads, including the original repeated-poll
  cycle debit and the updated last-debit byte.
- +0x91BC: DPCR/DICR control writes, partial-width merging and acknowledgement
  bits. The +0x8E4C dependency currently handles only its idle prefix; an
  actual requested transfer is not simulated or declared complete.
- +0x9C60: zero-valued timer initialization paths only. Nonzero mode changes
  and event rescheduling are the next reconstruction boundary.

Source evidence is the local hash-pinned POPS disassembly and, where available,
Ghidra function output. The ledger records these as partial except the reviewed
IRQ-reader body; none of the host runs establishes full emulator equivalence.

## Execution support

Unicorn remains the backend. Small generated memory thunks reduce round trips
through C during long BIOS copy/decompression loops; these are rehost adapters,
not additional reconstructed firmware functions. Every thunk checks its address
class before using the fast path and exits to the C dispatcher on a miss.
The RAM halfword-write thunk writes two bytes; it is not the byte-read thunk.
Original PRX pages and guest RAM remain nonexecutable. Only the generated
caches and explicit adapter page are executable.

The engine budget is now ten million instructions per invocation, still with
its ten-second engine timeout. The previous one-million limit interrupted a
progressing decompression loop before the next POPS cycle handoff. Budget stops
record registers and nearby generated words for diagnosis. Reported instruction
counts are observations of generated-cache instructions, excluding fast-helper
instructions, not a function-coverage metric.

The prior headless GE-list completion adapter is retained. It records requests
and advances host bookkeeping; it does not render a frame or validate PSP GPU
timing. Trace events are buffered until normal flush/close, so a forcibly killed
run can have an incomplete final line.

## Checked checkpoint

`out/ffvi_run.oxXg03/result/` reaches the timer-mode write at PS1 address
1F801114 from the decompressed RAM code. It records 9,084,533 generated-cache
observations and 1,551 entry transfers. The run remains diagnostic with PSP UI
bypassed, `game_executed: false`, and no rendered framebuffer or active audio.

Focused checks: `make test-native-emit test-native-events test-unicorn-cache`.
The cache test covers halfword stores and address-class misses. The event test
covers repeated IRQ polling and DMA-control acknowledgement. These are contract
checks and an integrated run, not exhaustive instruction-level equivalence.
