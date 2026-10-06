# DMA register reader

`rp_pops_dma_read` reconstructs POPS +0x9158..+0x91BB using the existing
`rp_core_dma_layout`. The reader is connected to both the direct helper and
dynamic I/O dispatch; its original PRX address is an exit from Unicorn.

The original increments the downcount by four before reading the register
shadow. That is a cycle refund, not a delay or a readiness override. Register
bytes and the DMA busy bit are returned unchanged at the selected width.

The pinned relocated table at +0xD45AC contains these six targets:
0x9194, 0x919C, 0x91A4, 0x91B4, 0x91AC, 0x91B4. They select signed byte,
signed halfword, word, unsigned halfword, unsigned byte and unsigned halfword.
Widths outside that table also use the unsigned-halfword path. Byte offsets
are retained when selecting the shadow; they are not rounded to a word.

Focused checks exercise all six entries, the default, nonzero byte offsets,
sign extension, the unchanged busy bit and wrapping downcount arithmetic.
The code uses `RP_DMA_ADDRESS` and the recovered clock accessors; no new raw
GP offsets or separate register storage were introduced. These checks are
not hardware timing or whole-emulator equivalence tests.

`out/dma-reader.I7pEHh/result/run.json` reaches the GPU DMA transfer callback
+0x12C74 after 39,303,523 generated-cache observations and 39,019 transfers.
The program has read CHCR and requested linked-list mode 0x01000401. This
confirms the reader is no longer the blocker; `game_executed` remains false.
