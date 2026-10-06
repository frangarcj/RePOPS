# CD DMA: transfer, accounting and completion

This pass reconstructs the path reached after the first CD sector becomes
available. It keeps DMA register writes, the device transfer callback and
completion as separate operations; writing bytes to RAM alone is not treated
as a completed DMA request.

## Original entries and native types

- +0x92A4 writes channel control (or delegates partial-width writes to +0x8AA4).
- +0x8E4C checks enable/priority, computes the byte count and calls the channel's
  registered transfer callback. The recovered device branch accepts +0xCE18.
- +0xCE18 consumes the CD FIFO, updates its cursor/status, invalidates affected
  compiled-RAM lookup entries and writes guest RAM. It returns the requested
  transfer size, including the original last-byte fill for a FIFO underrun.
- +0x8BB8 postpones already active channel events matching its mode mask.
- +0x8B1C completes a channel, clears busy/block count and signals the DMA IRQ
  when enabled. Pending-channel arbitration at +0x8D88 remains a boundary.

`src/native/pops_dma.h` names the 0x1C-byte channel state and 0x10-byte register
stride. The core has seven channel states at +0x1E8, a pending mask at +0x2AC,
channel registers at +0x2080, priority at +0x20F0 and interrupt control at
+0x20F4. JSON descriptions mirror those native layouts. New code uses member
addresses instead of new anonymous GP offsets.

The transfer-mode field includes the control bits at CHCR[10:8], so it must
not be mistaken for a newly invented two-bit enum. The priority scan and cycle
accounting retain the original bit operations. Transfer callbacks for other
devices and GPU-specific cancellation remain explicit stops.

## Copy and compiled-code invalidation

The native CD callback preserves the original trigger-bit check. The reached
aligned, bounded transfer maps to PS1 RAM at 0x09800000 plus the masked address.
Before copying it follows the original compiled-page bitmap to invalidate the
corresponding RAM lookup entries at a 0x400000 address bias. Small clear loops
write zero; the vector clear path uses the actual reset R403 row. PSP cache
instructions themselves remain a host-coherent adapter, not cache simulation.

The FIFO cursor is clamped to its limit and data-ready/request flags clear on
exhaustion. If fewer bytes remain than requested, the tail repeats the final
copied byte as the original does. Unaligned partial-word tails and spans beyond
the modeled RAM interval are still refused instead of guessing their behavior.

## Focused tests

`make native test-native-cdrom test-native-events` passes with the existing
sanitizer configuration. The added scenario checks an initially disabled
channel, DPCR enabling it, a 2048-byte copy, full byte comparison, stale lookup
entry clearing, MADR/BCR/CHCR updates, the 16387-cycle charge for that fixture,
completion IRQ flags and FIFO exhaustion. Another fixture verifies nonzero
last-byte fill when the requested transfer exceeds remaining sector bytes.

These are native-contract tests plus the integrated run recorded in progress.md,
not whole-DMA binary equivalence or a validation of every device callback.
