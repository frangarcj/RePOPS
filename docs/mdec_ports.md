# MDEC control and status

The port reader +0xF6DC, writer +0xF70C and existing reset routine +0x1B678
share a typed stream layout at core +0x620. The reset clears exactly 0x2c
bytes, after unlinking a pending event at +0x63c; initialization reinstalls
the DMA/I/O handlers, copies the original seven-word table and restores
the +0xE8F8 event callback. Neighboring memory is preserved.

The status reader ignores address/width, as the original does. It uses a
signed pending-byte comparison and an unsigned word count, combined with
ADDU rather than simply OR. Data writes only latch the command; control
writes reset only when bit 31 is set. These functions are not a decoder.

`test-native-mdec` checks table bytes, registration, command/status cases,
reset neighbors and unlink-before-clear order under ASan/UBSan. The actual
scheduler is tested separately; the MDEC fixture isolates its call contract.

`out/mdec-ports.KvdIhA/result/` confirms the reset and status read, then
latches command 0x40000001. The next boundary is DMA input callback +0xF54C:
209,054,869 generated-instruction observations and 2,466,732 transfers.
Both volatile card slots are selected and receive command 0x52, but neither
records a completed sector-read transaction in this trace.

No macroblock decoding, GE execution, framebuffer or game boot is claimed.
