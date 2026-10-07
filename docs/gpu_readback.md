# GPU readback: command, buffer helper and DMA

The compact diagnostic now retains the mode, command word and source range
of unsupported GPU packets. The reached word was 0xC0000000 (mode 6), not
a polygon subtype. It is the three-word VRAM-to-CPU transfer header.

The +0x15508 branch of +0x133D0 is reconstructed with a typed packet:
command, source position and extent. It masks the source to ten/nine bits,
normalizes zero dimensions to 1024/512, stores the transfer state and selects
GPUREAD mode 16. It returns immediately with the current list cursor and
accumulated work, without consuming following command words. The pixel cursor
and read latch are not reset by this header.

The focused GPU fixture checks partial header buffering, normalized dimensions,
preserved state and the subsequent explicit read-data boundary. Admitting a
readback command is not producing its pixel data. Until the readback path and
GE execution are available, returning invented zeroes would hide the actual
dependency and is not part of this reconstruction.

`out/gpu-boundary.wWWBMp/result/` identifies the command as mode 6 and word
0xC0000000. With its header reconstructed, `out/gpu-readback.7pExuj/result/`
stores source (512, 256), width 64 and height 256, then reaches the separate
DMA readback branch +0x12F90. It records 356,838,031 generated-instruction
observations and 2,540,027 transfers, with `game_executed: false`.

## Recovered buffer helper (+0x130BC)

`pops_gpu_readback.c` reconstructs the two transfer paths and query filling.
This entry writes a buffer; the scalar port passes its data latch and four
bytes, while the DMA path passes CPU RAM. The original decompilation failed,
so the instruction listing was reviewed and every word in this function was
checked against the pinned relocated image; there were no differences.

Mode 16 selects the direct GE path only for a 16-byte-aligned destination,
width divisible by eight, a rectangle within the 1024x512 surface and a
request large enough for the entire rectangle. Six original GE words are
emitted before a synchronization call. Other rectangles use mode 17 and
read halfwords row by row, wrapping physical X/Y coordinates independently.
The logical pixel cursor survives partial reads.

Completion details are deliberately not replaced by a generic PS1 reader:

- The CPU path preserves the existing upper halfword when its last pixel
  fills only the low half of a destination word, and repeats that packed
  word for surplus words in the same request.
- The direct GE path sets the read selector and transfer latch to 255; a
  surplus tail is filled with the value 255, not the last transferred pixel.
- An incomplete CPU read leaves the transfer latch at 17. A later call with
  selector 255 returns 255 rather than replaying the former latch.

The DMA entry clears the corresponding RAM-code validity words before calling
the buffer helper and returns the original byte request. It does not invent
a GPU-ready event or copy the SPU transfer's completion policy.

## Synchronization and verification limits

`rp_ge_readback_restart_list` represents the direct GE sync/enqueue pair;
`rp_ge_readback_barrier` represents the POPSMAN 7014C540 service. These are
explicit host-backend dependencies, not reconstructed POPSMAN bodies. The
live headless versions stop rather than acknowledge untouched EDRAM as valid
pixels. Zero-length, non-word-aligned and inconsistent-state domains also
stop instead of reproducing unsafe original loops.

`make test-native-gpu-readback` verifies the six GE words, nonzero pixel data,
partial reads, X/Y wrap, odd-tail preservation, surplus words, latch changes,
list restart order, DMA tag clearing and refusal without a completed backend.
Fixtures supply known completed pixels; they are not a renderer or a hardware
equivalence proof. The existing GPU suite still checks port buffering and
queries through the shared helper. Both targets pass under ASan/UBSan.

`out/readback-buffer.rLqHk4/result/` reaches the direct path on the real PBP:
32,768 bytes to 0x099A6200, old list id 185, continuation 0x49A00160. It stops
at +0x13148 before consuming unsynchronized pixels, with
`game_executed: false`. No GE renderer has been substituted with CPU GP0 drawing.
