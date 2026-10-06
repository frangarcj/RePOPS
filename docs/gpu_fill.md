# GP0 fill and GPU-ready event

The +0x134E0..+0x136C8 branch of +0x133D0 now consumes the three-word
GP0(02h) packet. It reproduces zero-size handling, the compatibility-dependent
X alignment, temporary GE scissor, masked fill color, original clear-template
call and restoration of the drawing scissor. The decoder advances by three
words, so packet data is not misinterpreted as subsequent commands.

The 32 eight-byte texture-cache records are named in `pops_gpu.h`. The fill
consumer and the existing initializer now use the same storage/group/flag
fields. Invalidations preserve the original 3+3+3+3+3+1 grouping and vertical
bank selection; invalid topology stops as a diagnostic instead of looping.
Display intersection and the original pixel-work shift are retained.

The focused GPU fixture checks seven exact GE words, original draw-state
restoration, a 16-unit work result, the three affected cache records and a
zero-width consumed no-op. GPU/event checks and the native build pass. These
are contract checks, not rasterization or whole-emulator equivalence.

`out/gpu-fill.eDODWV/result/` executes an actual 640x480 fill and records
9,600 work units before reaching GPU-ready event +0x125F0. The event's recovered
prefix sets the original status bits and either dispatches the existing
display refresh or returns idle. A positive pending GE submission remains an
explicit boundary at +0x12624; no provider return value is invented.

With that event connected, `out/gpu-fill-ready.6tT86C/result/` records its
completion and continues to the CPU emitter's signed-half state policy,
`signed_half_state_policy_not_reconstructed` at +0x4930. The final counters
are 39,392,837 generated-cache observations and 39,102 entry transfers. This
is an integrated idle-ready branch check; the pending-list branch is not
claimed to pass.

GE words are still commands in guest memory. No host framebuffer is produced
by this increment, and the diagnostic continues to record `game_executed: false`.
