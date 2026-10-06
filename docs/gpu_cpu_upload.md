# CPU-to-VRAM upload and pending GE submission

The partial +0x133D0 reconstruction now accepts GP0 A0h headers at +0x154BC
and retains command mode 9 until pixel data arrives. It implements the GE
rectangle and row paths from +0x157D8..+0x15D50, including odd row widths,
source alignment, Y wrap, cache effects and partial single-row continuation.
The header does not itself move pixels; a word that resembles a GP0 opcode
remains pixel data while the upload is active.

The ordinary CPU-to-GPU DMA block path at +0x12E98 shares this receiver.
It returns the original byte count, publishes the GE stall and schedules the
GPU-ready event using transferred words, not the linked-list cost formula.
The compatibility flag 0x10 invokes +0x125F0 and retains its special return
of one. The pre-existing port-buffer prefix is handled as described below;
GPU-to-RAM remains a boundary.

POPS +0x12624 and +0x128C8 both invoke provider 7014C540. The latter tests
the mode saved before consuming the port packet: its upload must be submitted
before the port's live source buffer is reused, even when mode 9 just finished.
The host adapter models the provider's enqueue/sync fallback by writing
FINISH at the old cursor, END at cursor+4, and publishing a synthetic list id
and stall. It does not implement the provider's MMIO polling fast path or
execute the GE. In particular, a submitted upload is not proof of updated
host-rendered VRAM or a displayed framebuffer.

The idle refresh branch +0x1252C uses the existing deterministic headless
vblank service and the shared timing tail, without emitting/releasing a GE
list. It updates frame/audio origins and the existing idle/rate state.
Only the on-time internal-display case is reconstructed; PSP real-time waits,
late vblank and external-display behavior are not claimed.

Focused GPU fixtures verify exact words, untouched pixel payloads, odd rows,
96+24-pixel continuation, submission before buffer reuse, DMA source selection
and the ready-event compatibility gate. The GPU/display/events/CD checks pass.
These are contract checks, not hardware equivalence. Integrated diagnostics
and the next reached boundary are recorded in progress.md.

## Mixed port/DMA and CPU pixel stores

The +0x12F30 prefix path appends a small DMA block to the existing port buffer
when their combined size is strictly below 192 bytes. Otherwise it drains
the existing prefix through +0x133D0 before consuming the DMA source. Both
paths preserve the original DMA byte return and delay, excluding the prefix.

Partial multi-row uploads, X wrapping or the mask-set condition select the
original +0x15D54..+0x15E6C CPU path. The dimensions alias becomes an absolute
end position, the cursor copies the upload origin, and the provider boundary
is captured even if no prior transfer was marked pending. At +0x15E70 native
C writes low then high halfwords into the original uncached VRAM view, with
independent physical X/Y wrapping. It preserves crossing a row between the
two halfwords, mask OR and ignoring the final padding halfword of an odd image.
These are reconstructed POPS memory writes, not host drawing from GP0.

Fixtures cover combined small prefixes, a drained prefix followed by a
50x4 image, and a masked 3x3 image wrapping both VRAM axes. They inspect actual
backing memory and untouched padding. The integrated pgEy9j run completes
the reached CPU upload and moves on to GP0 64808080 (textured rectangle).
The synchronization provider is still a headless fallback adapter; neither
this memory check nor the integrated run proves full GE coherence/rendering.
