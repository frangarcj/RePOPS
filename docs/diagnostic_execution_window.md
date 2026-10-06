# Native execution window and direct I/O entries

The host dispatcher had a fixed limit of 500,000 iterations alternating
generated-code execution and native helper calls. REPOPS_RUN_STEPS makes
that existing diagnostic limit explicit and adjustable (default 500,000,
accepted range 1..10,000,000). It is not a PS1 cycle limit and does not change
event deadlines, counters, device state or emitted instructions. The selected
value is recorded at entry; reaching it still reports a diagnostic stop.

Example of the longer investigation run:

```sh
REPOPS_RUN_STEPS=2000000 python3 scripts/run_native.py \
  --image build/native_image \
  --pbp input/games/final_fantasy_vi/EBOOT.PBP \
  --out out/diagnostic-long-new/result --diagnostic-skip-ui --timeout 600
```

The per-call Unicorn instruction bound is unchanged. The optional per-call
wall-clock timer is now disabled by default to avoid repeated host timer
creation; REPOPS_UNICORN_TIMER=1 restores it. The runner still enforces its
global timeout. See diagnostic_speed.md for the measured scope. Host timeout
and dispatcher iterations are separate limits; neither is evidence that the
guest is stuck or that a game successfully booted.

The longer window reached a direct generated-code call to +0x9850 with A0
containing 0x1074. The IRQ reader already existed in native C but was connected
only through generic memory helpers. Its direct dispatcher entry now returns
the existing helper's value in V0 and transfers to RA. The generated caller
handles storing/reloading the cycle count around the call, as for other direct
I/O helpers. +0x9850 is also an explicit Unicorn exit, before any PRX execution.
The following direct DMA channel write at +0x92A4 is wired to its existing
native function with A0/A1/A2 as address/value/width. The installed I/O table
was checked against direct helper dispatch rather than copying the generic
memory-handler code into new implementations. This wiring is not another
interrupt/DMA controller or an additional reconstructed body.

Integrated results are recorded in progress.md. Event and generated-cache
contract tests pass; hardware equivalence and complete emulator execution
are not claimed.

`REPOPS_TRACE_COMPACT=1` omits high-volume instruction/helper and register
records for long reverse runs. It does not alter execution or counters.
Blockers, milestones, serial/CD state and execution configuration remain in
the trace; the original full trace is still the default. This is not suitable
as a replacement for a complete GE command capture.
