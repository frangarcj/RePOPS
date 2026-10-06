# SPU DMA transfer (+0x8698) and deferred callback (+0x8898)

The reconstructed transfer uses the existing ME shared sample RAM, not a
second sound buffer. It recovers the core halfword cursor, transfer IRQ latch,
deferred event and saved address/length through a named wire layout.

The source byte count and actual copied span are intentionally distinct.
At the end of sample RAM, the original copies only the tail span, advances
the cursor using the full requested length, and reports the full length
unless the compatibility flag selects 1. It does not issue a second copy
at sample RAM offset zero. A zero-length request returns 1 immediately.

Readback clears the original RAM-code validity words before copying sample
RAM to CPU RAM. The IRQ interval has the original extra 32 bytes on reads;
it sets the core latch rather than immediately manufacturing a guest IRQ.
PSP cache operations and both memcpy services are explicit coherent-host
adapters. Uncovered cursor/alignment domains still stop rather than invent
memory behavior.

A positive deferred-write setting schedules the event with four times the
byte count and stores a tagged address. +0x8898 calls the same transfer with
that tagged address, preventing the write from deferring itself again. The
original ignores the second call's returned byte count, as does the C.

`make test-native-spu-dma` checks transfers in both directions, code-tag
clearing, IRQ endpoints, truncated-tail behavior, compatibility returns,
deferred publication/copy order and the larger memcpy-service path under
ASan/UBSan. Event/serial and CD tests pass with the new callback routing.
The integrated result is recorded in `progress.md`; no audible game playback
or hardware-equivalence claim follows from copying sample data.

`out/spu-dma.lmurpi/result/` performs 510 writes to shared sample RAM,
930,672 bytes cumulatively (not unique resident bytes). It advances to an
unsupported GPU packet at +0x133D0 after 356,272,699 instruction observations
and 2,535,609 transfers. Only immediate writes are observed in that run;
readback and deferred-copy behavior are currently covered by the fixtures.

The same trace contains 8,945 completed packed-stereo mixer samples, with
4,978 nonzero values. These are observations from the integrated C run,
not synthetic mixer inputs or a listening test. The small aggregate is
saved beside `run.json` as `spu-observations.json` for trace retention.
