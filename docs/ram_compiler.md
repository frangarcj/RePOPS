# RAM compiler and second generated-code cache

`pops_compile_ram.c` adds the RAM setup/publication branch of +0x58C0 and
the +0x4B44 code-guard emitter. The record walk is shared with BIOS compilation;
RAM and BIOS have different empty-state comparison masks, while a join still
resets the known-zero mask to 0x80000000 in both paths.

The RAM mode matters independently of the source address. The current game
selects **0x8003**. Its bit 15 skips the initial invalidation stub/checksum
prologue; treating every RAM block as mode 3 would emit extra work. Optional
headers and per-entry guard wrappers retain their original flag conditions.
Guard-emission paths are reconstructed from the instructions but were not
independently compared with the original in this increment.

Unicorn can execute both generated caches: 09540000..097BFFFF for RAM-derived
code and 09B80000..09BFFFFF for BIOS-derived code, bounded further by their
published cursors. PS1 RAM at 09800000 and original PRX pages remain data-only.
Calls into native helpers use the same explicit exits. Cache invalidation
recompilation at +0x4E18 remains an explicit unimplemented boundary.

`out/ffvi_run.IaqNCM/result/` publishes RAM blocks of 32, 108 and 24 bytes,
executes the first two and reaches later RAM dispatch after BIOS calls. It
then asks to compile guest 00000600 and stops at the immediate-shift record
category (0x11), with 161,225 instruction-hook observations and 45 block-entry
transfers. No game boot or graphics/audio output is claimed.

`make test-unicorn-cache` now checks execution from the RAM-derived cache and
a transfer into the BIOS-derived cache, in addition to existing helper and
memory tests. Emitter and event smoke checks also pass. These are focused
host tests, not full original-vs-C or PSP hardware validation.
