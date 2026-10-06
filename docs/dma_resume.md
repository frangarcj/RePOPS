# Deferred DMA resumption

POPS +0x8CAC..+0x8D87 is reconstructed as `rp_pops_dma_resume` and registered
with the guest event dispatcher. It uses the existing channel, register and
event layouts. Callback selection is shared with the initial channel start;
the argument handling is not shared blindly: resumption forwards the stored
MADR/BCR/CHCR directly, as the original does.

The transfer callback's signed return determines the next action. A negative
word carries a continuation address, masked with 0x00FFFFFC; the event and
callback installed by the transfer routine are retained. A positive word
switches the event to +0x8B1C and schedules that completion after the returned
delay, with MADR cleared. Zero calls completion immediately. A disabled channel
does not transfer or schedule anything; GPU channel 2 first calls its recovered
ready event, then the stored callback changes to completion.

Focused tests use a scripted transfer return to check exact forwarded arguments,
retention of an event created by the transfer callback, delayed versus immediate
completion and both disabled-channel variants. The immediate case checks the
real completion debit and CHCR/BCR effects. Those are contract tests; they do
not prove hardware timing or all possible device callbacks. Existing CD/DMA,
GPU and timer/event checks still pass.

`out/dma-resume.C9bwg5/result/` passes this event in the integrated FFVI route
and reaches GPU command 0x80000000 in +0x133D0. It records 40,087,427 generated
instruction observations and 43,469 transfers; `game_executed` remains false.
The resumption body is complete at the stated scope, while device transfer
callbacks and DMA arbitration still contain their separately recorded partial
implementations. This does not claim full-emulator equivalence.
