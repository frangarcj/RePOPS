# CD controller: register interface and initial commands

The native path now reaches POPS +0xD1B0/+0xD088 instead of stopping at the
generic byte-store helper. `pops_cdrom.h` names the controller's 0xC0-byte
state, its 0x1C-byte response records and 0x10-byte cache nodes. New accesses
use those fields in C; they do not cast guest memory to native pointers.

Recovered behavior includes bank selection, four-byte parameter storage,
response/data reads, interrupt masking/acknowledgement, transfer cursors,
volume matrix writes and the reached command paths. A command response is
scheduled through +0xC3A4/+0xC418 and published by +0xC268. An outstanding IRQ
keeps the other response pending until acknowledged. The general scheduler now
passes the actual event address to CD callbacks, reusing the common IRQ raiser.

`Setloc` validates packed-BCD parameters and uses the existing +0xADE8 converter.
The normal-data +0xD41C path searches/reorders the cache or queues an event for
the CD worker. This is **not** completion of a disk read. `SeekL`/`SeekP` use the
reconstructed +0xC480 timing calculation and separate acknowledgement/completion
events. The original SQRT.S term uses a factor of 6000; raw pointer-typed
pseudocode displayed 1500 and must not be copied literally.

The reset's event unlinking now uses the already reconstructed scheduler. Audio
pacing +0x11520 preserves its delay calculation, with a headless cooperative ME
adapter for the thread wait. Its 44.1-kHz sample scheduling is an explicit host
model, not PSP thread wakeup or cache-timing validation.

`make test-native-cdrom test-native-events test-native-spu test-native-me`
passes. Tests cover FIFO capacity, delayed Getstat, pending-response IRQ
chaining, signed byte reads, data cursor clamping, Setloc prefetch requests,
and zero/long-seek arithmetic. The ReadN fixture records the audio-sync call
as a boundary; the integrated run uses the actual reconstructed ME path.
Other unexercised reset/pacing dependencies are aborting stubs.

`data/state_layout.json` records the CD layouts for subsequent Ghidra imports;
this increment checks their extents and native static assertions rather than
repeating a full decompilation. Unimplemented commands, audio-prefetch paths
and the CD worker remain explicit boundaries. No game boot is claimed.

The integrated `out/cd-seek.grqSK4/result/` run completes Setloc for sector 4
and SeekL, publishing three responses and queuing one cache-block request.
It stops on Setmode 0x0E/0x80, with 38,182,482 generated-cache observations and
4,883 transfers. The intervening audio wait calls the actual reconstructed ME
producer for 574 samples in the host adapter; its 13,035-us request is preserved.
The CD worker's queued block has not been read or decoded by this increment.

## Setmode and ReadN follow-up

Setmode +0xB5EC..+0xB734 handles drive speed transitions and postpones an
already scheduled sector event. Its inline unlink differs from the general
remove helper: it does not adjust the core deadline before reinsertion.
ReadN/ReadS +0xBE48..+0xBFCC schedule the sector callback with the original
speed/seek delays and preserve outstanding-response rules. Neither path
fabricates a data-ready bit or marks the worker request completed.

The local fixture checks the fast speed-change delay of 10,000 cycles, the
separate acknowledgement at 0x4000, and ReadN's deferred callback. Integrated
`out/cd-readn.LUz1jf/result/` reaches +0xC5EC with 38,444,924 observations and
13,397 transfers. Retrieving the sector through +0xD5CC and the CD worker is
the next boundary. No completed sector I/O is claimed yet.
