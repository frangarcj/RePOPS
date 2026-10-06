# VRAM copy packets through GE

GP0 mode 4 now dispatches the four-word VRAM-to-VRAM copy packet. The reached
GE path is recovered from +0x14FFC..+0x15180, the single-row staging branch
at +0x152FC onward, and the shared cache invalidation tail. Source review
uses the pinned disassembly and existing +0x133D0 decompiler output.

The packet and cost shift are named in `pops_gpu.h`. The transfer-origin and
size fields are explicitly aliased as copy source/destination for this phase;
their storage does not move. Equal raw source/destination is a consumed no-op.
Width and height use the original subtract/mask/add normalization, so zero
means 1024 or 512 respectively, not an empty rectangle. Work uses the original
signed shift configuration and does not substitute a completion status.

For nonoverlapping in-range rectangles without mask writes, POPS emits its
five-word transfer setup and template call. An overlapping single row stages
through EDRAM offset 0xD0000 and emits a second transfer; draw-mode invalidation
is retained. Destination-cache invalidation reuses the typed grouped traversal.
Copies requiring the synchronized CPU read/write path, and the occupied-cache
single-pixel shortcut, stop explicitly. This is GE production, not execution
of a framebuffer copy by the host.

The native GPU fixture checks all five words of a nonoverlapping copy, original
source/destination state, work and cache effects, the ten-word staging sequence,
equal-address no-op, zero-width normalization with a negative cost shift, and
the mask-enabled CPU-path boundary. Display and DMA/event checks still pass.
These are contract tests, not GPU rendering or hardware timing verification.

The first integrated attempt, `out/gpu-copy.WwGIOx/result/`, timed out at
120 seconds before this new path. Its 159,053 complete non-clock trace records
match the previous run's prefix; the host was also running other compilations.
No guest timing or readiness response was changed to bypass the timeout.

The repeated run `out/gpu-copy-retry.Jaxybd/result/`, with a 300-second host
budget, completes and reaches GP0 A0h. It records 55,418,185 generated-code
observations and 86,645 transfers, with `game_executed: false`. The trace has
32 active-display preparations, 8,680 flat-polygon records, 250 tagged DMA
continuations and 31 continuations entering completion. There are no emitted
VRAM-copy milestones in this run: it passes the mode-4 gate/early-return path,
not the newly tested nontrivial GE transfer path. Do not describe it as an
executed VRAM copy or a rendered frame.
