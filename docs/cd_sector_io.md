# CD block loading and first data-sector delivery

Target: the hash-pinned PSP POPS 6.60 analysis image. This increment follows
+0xDA3C (data worker), +0xD5CC (sector lookup) and +0xC5EC (sector event).
The PBP is a local input; neither firmware nor game bytes are committed.

## Recovered path versus format adapter

The worker invalidates its least-recent cache node before reading, publishes
the block's first sector and cache head only after success, clears the pending
request and signals completion. Each cache block holds 16 * 2352 = 37632 bytes.
The host runs this data-worker iteration cooperatively when the original
blocking cache lookup needs it. Thread timing, the compressed-input 512-byte
reuse optimization and the audio-worker branch are not reconstructed.

The actual local FFVI PBP is already admitted by the existing plain PSISOIMG
provider (DATA.PSP starts with an ELF header and the disc header is readable).
Its block zero at file offset 0x110000 contains 8099 bytes of raw DEFLATE.
Python's zlib probe and the native adapter both expand it to 37632 bytes.
The first native full run is `out/cd-sector.fwAsEX/result/`: it publishes block
zero and delivers sector 4, then stops at the dynamic register-shadow write.

`pops_cd_block.c` uses system zlib ONLY as a format adapter for that admitted
plain PBP path. This does not reconstruct, execute or substitute a claimed
implementation of Sony's +0xE02C range decoder. Protected/unknown providers
are still rejected. Empty and stored blocks use the corresponding zero-fill
and direct-copy branches. A failed/truncated compressed stream never publishes
a cache hit or successful data sector. Integrity/authentication services remain
outside this adapter; a successful inflate is not an integrity proof.

## Cache and sector event

The normal +0xD5CC path searches the original linked cache, services a queued
read on a miss, promotes the node, applies the optional sector transform and
requests the following block. Transform 2 reverses the complemented four-bit
slot index. Nonzero transform values rebuild sync/MSF bytes while preserving
the sector's mode byte, following the original path.

The +0xC5EC path checks the real sector sync, sets the header pointer, publishes
the alternating Mode-2 buffer, prepares the byte window and sends the existing
secondary response with IRQ kind 1. It schedules the next sector using the
mode/compatibility timing. XA routing, read-error recovery and end-of-disc
responses remain explicit original-address boundaries.

New native layouts are `rp_cd_block_index_layout` (32 bytes) and
`rp_cd_sector_header_layout` (24 bytes), mirrored in `data/state_layout.json`.
The formerly anonymous controller byte +0x8E is `sector_defer_count`, consumed
by the bounded postpone branch. Existing events/cache/IRQ types are reused.
No new known GP fields are accessed through anonymous offsets.

## Next memory-control boundary

The first sector reaches a write at PS1 0x1F801018. Its actual table entry is
+0x8AA4, a shadow-register writer, not a new disc codec. The recovered helper
stores byte/halfword/word for selector 0/1/2 and returns without writing for 3.
The dynamic word-store bridge now calls it. A wire union exposes the 4-KiB
register shadow and the existing I_STAT/I_MASK members without changing offsets.

## Focused checks

`make native test-native-cdrom test-native-events` covers command/response and
seek scheduling plus compressed block -> cache -> event -> FIFO, full output
byte comparison, a cached second sector, truncated-input nonpublication and
shadow-store widths/no-op selector. The tests use ASan/UBSan where configured.
These are focused native-contract checks and a real-PBP integrated path, not
whole-worker binary equivalence or PSP timing validation.

The runtime remains reconstructed C with Unicorn executing only generated CPU
cache code. First sector delivery is not a claim that FFVI has booted.
