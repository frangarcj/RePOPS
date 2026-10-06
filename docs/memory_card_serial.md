# Memory-card serial protocol (+0xA508)

The ordinary card route now uses a named `rp_memory_card_layout`: flags,
checksum, sector address, command/status, the 0x80-byte container header,
and 1024 sectors of 128 bytes. Its 0x20088-byte stride and data offset 0x88
match the existing volatile-card worker. Serial reads do not create a second
card image or substitute canned sector contents.

The port's +0x9E64 callback routes the selected card slot to this body and
retains its response convention: low eight bits are received data, bit 8
terminates the transaction. Nonterminal replies still use the port's original
ACK event and interrupt-control path.

## Reconstructed ordinary path

The command header resets checksum/address state, accepts R/W, and assembles
the big-endian sector number from the two transmitted address bytes. The read
route returns the synchronization bytes, address echo, all 128 data bytes,
address/data XOR and tagged 0x147 terminator. Missing cards and invalid sector
numbers return the original tagged 0x1ff response.

Write payload and checksum processing are also recovered. The original writes
RAM before validating the checksum; an invalid checksum therefore returns
0x14e without undoing data already written. Sector 0x3f suppresses payload
writes. A valid ordinary write marks the card dirty and stops at the pending
save-alarm service (+0xA6AC), before returning a fabricated success. No user
savedata or VMP file is written by these changes.

The alternate slot-0 protocol selected by +0x1C964 remains an explicit boundary.
The ordinary serial body is therefore partial in the function ledger, even
though complete ordinary read transactions execute.

## Verification

`make test-native-memory-card` covers reads from both slots, including sector
1023, invalid and absent cards, checksums, early RAM write effects, sector-0x3f
suppression and the successful-write alarm boundary. Tests run with AddressSanitizer
and UBSan. Native event/serial and CD tests also pass after integration.
These are focused contract checks, not full binary/hardware equivalence.

The integrated result is recorded in `progress.md`; a sector-read event is
not proof that the game has booted or that its display was rendered.
