# Serial I/O and controller reconstruction

`pops_serial.c` recovers the two 0x2c-byte serial ports and the 0x30-byte
controller response records. The reset and dynamic memory helpers use those
same layouts. Data writes schedule +0x9E64, which selects the protocol,
publishes the received byte, and schedules +0xA220 only when the protocol
has not terminated. The IRQ callback tests the original control bit.

The controller path supports the original 0x42 transaction. Its phase-zero
button sampling is an explicit neutral-input host adapter, not a recovered
PSP input provider. A nonzero extended-response flag selects six bytes;
the terminal byte still comes from the configured response-length index.

## Pending-change review

The review against the relocated image and original control flow corrected:

- +0x9FB0 consumes the current downcount, not the previous sample timestamp.
- +0xA138 clears the phase of both ports in a branch delay slot. Only port 0
  receives the replacement status 5.
- +0xA1B8 uses signed MIN for deferred debit. The reader's saturation is
  signed MAX after 32-bit subtraction.
- +0xA2D8 treats the extended-response field as a flag, not a byte count.

Focused native event fixtures cover those cases and a complete neutral
digital-pad transaction. The emitter fixture verifies the SLL/SRL-to-EXT
fold and known-value update. These are contract checks, not a claim of full
instruction-level equivalence or hardware validation.

The pre-review integrated run `out/serial-controller.1GWZpT/result/` reached
the memory-card protocol at +0xA508 after 199,993,180 generated-instruction
observations. It did not demonstrate FFVI execution or rendered output.
The reviewed run `out/serial-review.u0RiXh/result/` reaches the same card
boundary after 200,395,762 observations and 2,340,077 compiled transfers.
It used the explicit 10-million dispatch limit and stopped on +0xA508,
not on the host budget. Its neutral-input adapter is recorded in the trace.
