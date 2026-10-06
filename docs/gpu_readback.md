# GPU readback command admission

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

The next reverse boundary is +0x12F90 -> +0x130BC and its GE synchronization
contract. Reading the current shadow backing alone would not establish that
the previously emitted GE commands produced the pixels the guest requested.
