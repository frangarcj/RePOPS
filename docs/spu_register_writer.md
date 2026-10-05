# SPU register writer

`src/native/pops_spu_registers.c` reconstructs +0x7F00 from its instruction
listing and `00007F00_spuWriteRegister.c`. It preserves byte-write zeroing,
low/high word-write order, cycle debits, the small sample-address adjustment,
voice/key masks, shared producer flags, FIFO transfers and IRQ event requests.

The writer and the existing ME callback share the same backing memory.
A 22-us PSP thread delay is an explicit cooperative-worker adapter, not
hardware timing. A busy ME critical section still stops with its pending
condition. Active-voice mixing has not been added by reconstructing the writer.

`make test-spu-registers` checks selected register paths with scripted event
services. `make test-native-events` also checks removal/deadline adjustment.
These tests are not instruction-level equivalence or audio-output validation.

`out/ffvi_run.3AJUvN/result/` writes zero to offsets 0x180, 0x182, 0x184 and
0x186 through the C writer, then reaches guest BIOS PC BFC06EC4. Compiling that
region stops at the specialized dynamic-base path +0x3CA8. The run contains
29,228 generated instruction-hook observations; FFVI has not booted.
