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

## DMA input (+0xF54C)

Command 2 prepares 128 binary32 factors, interleaving the two 64-byte input
banks. It uses the original 64-halfword weight table at module +0xD49B8,
the special first coefficient shift and the two original floating constants.
The fixture checks exact result bits in both banks, including nontrivial
rounding and endpoint coefficients. Command 1 publishes input byte counts
and the input address; a pending output request still stops at +0xE8F8.

`out/mdec-input.gBy4ri/result/` actually executes the 128-factor setup. It
then reaches the PAL display correction at +0x125A4, after 209,714,268
instruction observations and 2,477,090 transfers. The scale-table command
passes through as in the original input callback. Decoded video is not shown.
