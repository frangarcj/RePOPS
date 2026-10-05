# Native ME worker checkpoint

`src/native/me_worker.c` reconstructs the control/data flow of corpus POPSMAN
`+0x2F88..+0x31EB` as a cooperative C state machine. The corpus hash is
`ff4222e8085190e1f357aece096282963e0e8e317b76547fafb1600a746a237e`.
Provider offsets and absolute bus addresses use separate callbacks.

Implemented paths include output setup, 24-word prefill, previous-sample
submission before the sample callback, halfword override, fade, exact control
acknowledgement, and parked states. An unavailable sample callback is explicit:
it is not replaced by generated silence or a success acknowledgement.

The worker remains isolated from FFVI. The POPS sample callback at offset zero
is not reconstructed. Device state is supplied by the host; HALT/wake behavior
is approximated by cooperative polling, not PSP interrupt or timing emulation.

`make test-native-me` passes the existing scripted bus tests with sanitizers.
The earlier optional comparison in
`out/me_worker.jUeIz8/drain_verification_01/verification.json` passed 1,712 cases
across -O0/-O2 for **only +0x3164..+0x31C7**, using scripted MMIO and a replaced
DDR service. It is not whole-worker equivalence or verified sound output.

The read-only corpus export in `out/me_worker.jUeIz8/provider_code/` matches
the older provider's relocated worker bytes and resolves its callback slot to
`+0x4C5C`. That does not equate the two complete provider modules.
