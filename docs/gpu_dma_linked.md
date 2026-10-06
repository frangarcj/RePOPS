# GPU DMA linked-list path

The native +0x12C74 reconstruction now implements the linked-list branch
through +0x12E94. Non-linked transfers retain an explicit boundary at +0x12E98.
It reads the original node header from PS1 RAM, extracts payload length and
the next address, and feeds the existing +0x133D0 packet consumer directly.
The GP0 port's assembly buffer is not modified or used as scratch for DMA.

This preserves the intended boundary: POPS builds GE words in guest memory;
the platform observes queue/stall operations. POPSMAN E7F06E2B remains an
explicit captured host service, not a claimed native provider or renderer.

The callback accounts for node overhead and scaled packet cost, removes a
pending ready event when present, schedules the terminal ready event, and
retains the tagged continuation for self/two-node loops or a spent slice.
Negative continuations schedule +0x8CAC; that later callback is not reconstructed
by this increment. The shared +0x8BB8 helper remains one function in the ledger.
A bounded host diagnostic guard reports an excessive chain rather than hanging.

New native fields name GPU DMA cost scaling and the deferred frame debit in
`pops_gpu.h` and `pops_dma.h`. The original 32-bit addresses, wrapping arithmetic
and structure strides are retained. This is not a host-pointer rewrite.

## Checks and execution

`make native test-native-gpu test-native-events test-native-cdrom` passes.
The GPU fixture checks a two-node chain, exact GE state words, terminal event
arguments, 17-cycle debit, untouched GP0 buffer and a self-loop continuation.
Its scheduler and no-active-channel delay adapters isolate these contracts;
separate event/CD tests exercise the shared implementations. No exhaustive
instruction equivalence, asynchronous GE timing or rasterization is claimed.

`out/gpu-dma-linked.qooyux/result/run.json` records a real DMA payload with
header 0x05FFFFFF. E6/E1 state commands are processed and two GE words emitted,
then GP0(02h) stops at +0x134E0. The transfer has not completed at that stop.
Counters remain 39,303,523 generated-cache observations and 39,019 transfers;
the report keeps `game_executed: false`.

The next vertical task is the reached fill-packet branch, preserving its GE
representation and packet-length handling. Other primitives, block DMA and
continuation/ready callbacks must not be treated as implemented by this path.
