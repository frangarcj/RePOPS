# Native ME worker checkpoint

`src/native/me_worker.c` reconstructs the control/data flow of corpus POPSMAN
`+0x2F88..+0x31EB` as a cooperative C state machine. The corpus hash is
`ff4222e8085190e1f357aece096282963e0e8e317b76547fafb1600a746a237e`.
Provider offsets and absolute bus addresses use separate callbacks.

Implemented paths include output setup, 24-word prefill, previous-sample
submission before the sample callback, halfword override, fade, exact control
acknowledgement, and parked states. An unavailable sample callback is explicit:
it is not replaced by generated silence or a success acknowledgement.

The worker is now reached from the opt-in core diagnostic through
`src/native/pops_me.c`. The POPS sample callback at offset zero is still
unimplemented at this checkpoint. Device state is supplied by the host; HALT/wake behavior
is approximated by cooperative polling, not PSP interrupt or timing emulation.

`make test-native-me` passes the existing scripted bus tests with sanitizers.
The earlier optional comparison in
`out/me_worker.jUeIz8/drain_verification_01/verification.json` passed 1,712 cases
across -O0/-O2 for **only +0x3164..+0x31C7**, using scripted MMIO and a replaced
DDR service. It is not whole-worker equivalence or verified sound output.

The read-only corpus export in `out/me_worker.jUeIz8/provider_code/` matches
the older provider's relocated worker bytes and resolves its callback slot to
`+0x4C5C`. That does not equate the two complete provider modules.

The integrated run `out/ffvi_run.M0dYHv/result/` initializes serial/CD/timer
state and SPU data tables, registers the callback, and reaches its first call.
The host assigns offset-zero callback the logical identity `0x08800000`; that
is not a measured PSP load address or executable host pointer. It retains the
stack `0x09FF8000` and the registration routine's OR-patch behavior.

An explicitly headless, immediately-ready output sink replaces hardware
startup. The worker writes 24 prefill zeros and one previous sample before
asking for the unimplemented callback; those 25 words are not game audio.
ACK remains zero. The old input binaries and databases are untouched.

Only the RAM aliases `0x48000000..0x49FFFFFF` are collapsed onto the existing
native RAM region. This makes shared state visible to both C components, not
a cache-coherency implementation. The existing memory smoke test checks this
alias together with module/scratchpad separation.
