# Guest event dispatch and BIOS continuation

`src/native/pops_events.c` reconstructs the reached path of POPS +0x953C.
The +0x1A68 handoff saves the guest PC, compiled return address and cycle
downcounter before dispatching. +0x1A80 instead dispatches from the guest-PC
cache after the same event pass. These are C paths, not emulated PRX code.

The scheduler removes each due node before invoking its callback and keeps
the original `deadline - now <= 1` threshold. Callback cycle debits advance
the effective guest time; overshoot is retained when computing the next wait.
Negative reset control, cache invalidation and ordinary interrupt-vector
preparation call existing C. GTE completion during interrupt entry and an
expired empty-list sentinel remain explicit boundaries.

The initial callback +0x1265C computes the next frame phase with a single
precision product and nearest-even rounding. It reaches +0x15F54 in the
diagnostic; the optional +0x15FE4 phase is also represented. Enabled timer
gates and unhandled video callbacks are still boundaries.

`out/ffvi_run.jDWXL2/result/` dispatches the event at cycle 49,509, schedules
the next wait at 516,602 remaining cycles and resumes the RAM-clear loop.
It then reaches the SPU register writer +0x7F00 after 29,203 generated-code
hook events. This is BIOS startup, not FFVI gameplay or working audio.

Focused check: `make test-native-events`. This uses one scripted callback to
check unlink order, overshoot and debit accounting, not binary equivalence.
Integrated check: `REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh`.
