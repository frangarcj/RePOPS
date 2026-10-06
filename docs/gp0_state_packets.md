# GP0 framing and state-command emission

Original ranges: POPS +0x127D8..+0x12988 (port framing), +0x133D0 and
+0x13468..+0x134DC (packet dispatch), +0x136D4 (texture-cache clear), and
+0x15554..+0x157C0 (drawing-environment state). All offsets refer to the
hash-pinned POPS reference, not POPSMAN.

## Recovered path

Native GP0 writes clear the original busy bits, consult the module's command
length table and collect words until the packet is complete. The consumer
currently handles the control/no-op group and drawing-environment commands
E1 through E6. Native GP1 handling includes command-buffer reset and DMA-mode
selection; the full reset and other controls remain explicit boundaries.

The state commands preserve the draw mode, texture window, clip rectangle,
drawing offsets and mask state. GE commands are written into the original
list buffer with the original values and cursor increments. For drawing
offsets the original float conversion and word rotation are retained.
No direct PS1-to-host drawing was introduced. See module_boundaries.md.

The command buffer view has 48 word slots: its final word overlaps the data
read latch at core+0x35BC. The packet-extra-word byte at core+0x366B overlaps
the high byte of the existing display-mode word. Both are explicit unions in
pops_gpu.h; this does not claim simultaneous independent storage. Assertions
check these offsets, the pending-event node and the collector count.

## Checks and limits

Focused GPU tests retain the GPUSTAT cases and check exact GE output words,
state changes, signed drawing offsets and packet buffering. A four-word
triangle remains buffered for three writes; the fourth reaches the explicit
unreconstructed primitive boundary without setting ready bits. Local scheduler
callbacks abort if unexpectedly called in these zero-delay fixtures; actual
scheduling is covered independently by event tests and the integrated path.

Existing headless list sync/enqueue behavior remains an adapter. Recorded
queue requests or generated GE words are not proof of rendered pixels or of
reconstruction of POPSMAN's corresponding service implementation.

`out/gp0-state.prJDrA/result/run.json` passes the first GP0 word, 0x0004FAA8,
which the original decoder treats as a no-op. The next command is GP1 reset
(port 0x1F801814, word zero), stopping at +0x12988. It records 39,246,445
generated-cache observations and 39,001 entry transfers; game_executed remains
false. This actual invocation emits no new GE words; the state-command GE
words are verified by synthetic fixtures, not by a game framebuffer.
