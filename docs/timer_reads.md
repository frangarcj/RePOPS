# Timer register reads and native layout

POPS 6.60 +0x9BE0..+0x9C5F is reconstructed in
`src/native/pops_events.c:rp_pops_timer_read`. Its three-counter state is named
by `pops_timer.h`; the reader, synchronizer, scheduler and touched writer use
the same 0x20-byte layout. Numeric guest addresses remain distinct from host
pointers.

The original calls +0x9B6C when the target/flags word is nonnegative;
otherwise it returns the stored count from the origin field. Width 1 sign
extends counter reads. Mode reads override the result with the old mode word,
then clear bits above bit 9, without applying that sign conversion.

The dispatcher connects both the direct helper and the dynamic I/O handler.
Original PRX pages remain nonexecutable in Unicorn.

Checks: `make native test-native-events test-unicorn-cache` passes. The timer
fixtures cover signed/unsigned counters, paused counters, target wrap, clock
division, mode/status clearing and unchanged downcount. They are contract
tests, not whole-emulator or hardware-timing equivalence.

`out/timer-read.2JJRXM/result/run.json` records the integrated path reaching
the next unimplemented reader at 0x1F8010A8 after 39,303,310 generated-cache
observations and 39,018 transfers. It still reports `game_executed: false`.
