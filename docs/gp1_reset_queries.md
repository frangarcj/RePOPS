# GP1 reset, display controls and scalar GPU reads

Target: pinned POPS 6.60. New native C and field accesses are in pops_gpu.c
and pops_gpu.h; the previous startup-only reset in pops_graphics.c now calls
the same port writer. There is no second divergent reset implementation.

## Original sequence and backend boundary

At +0x129B4..+0x12AB8, reset changes the module display-transition state from
1 to 2, clears the transfer flag, and checks for an existing list. The
+0x12ABC branch stores END at cursor+4 before FINISH at cursor, then calls
sceGeListUpdateStallAddr(old_id, 0). It queues the module template +0xD5008
and runtime template 0x041B9300, resets GPU fields, calls sceGeDrawSync(0),
and enqueues 0x49A00000 with stall at the same address. Packet mode/counts
are cleared last. These are direct sceGe imports, not provider 7014C540.

The existing state-list capture is still an explicit headless backend. It
records list contents and service ordering without claiming pixels or PSP
synchronization timing. The integrated reset closes a previously queued
list; it no longer aborts solely because an ID is present.

The byte-verified reset writes are retained with named GPU fields, including
the display-mode byte/collector union. Raw disassembly of the pinned data
image was necessary at +0x129B8/+0x12AE8/+0x12B2C: the historical text listing
contains misleading register names there. Its printed instruction words and
the actual image, not those names, establish the 1-to-2 state transition.

## Display and query paths

The reached writer now includes GP1 commands 3..8 and 16: display disable,
DMA direction, display origin and ranges, mode-byte update and query selection.
Their flags are only dirtied under the original change conditions. No frame
is drawn by these setters.

The scalar branch of +0x130BC uses the query table at +0xD53BC. Selectors 2,
3, 4 and 5 return texture-window, draw-area and drawing-offset state; selector
7 returns zero. Default scalar selectors return the selector itself. The
result is written to both the read latch and transfer latch. The port reader
preserves the original configured cycle debit. Modes 16 and 17 are VRAM
transfer paths and remain unsupported, not successful dummy reads.

## Checks and actual progress

`make native test-native-gpu test-unicorn-cache` passes. Fixtures check
reset state, preserved unrelated fields, old-list terminators before template
submission, the new stalled list, changed/unchanged display ranges and scalar
query results/latches. The fixture substitutes only the template capture;
the integrated run reads actual template bytes.

`out/gp1-reset.Uz610b/result/` passes reset and first stops at GPUREAD.
`out/gpu-query.GeOu9d/result/` then executes query 7 twice and reaches timer
I/O 0x1F801110, with 39,303,174 generated-cache hook observations and 39,015
entry transfers. These are contract checks and host diagnostics, not proof
of a rendered framebuffer, hardware equivalence or FFVI boot.
