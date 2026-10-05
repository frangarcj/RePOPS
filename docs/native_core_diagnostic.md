# Explicit core-initialization diagnostic

```sh
REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh
```

This deliberately bypasses the unreconstructed PSP startup UI at +0x28DF8.
It is not a menu implementation or a claim that all menu-created state exists.
No savedata/config output is fabricated. Normal execution still stops at the
UI boundary. Both trace and run.json report `diagnostic_ui_bypassed`.

The diagnostic queues the memory-card thread without executing its body,
initializes controller records, builds the 32,768-entry reciprocal table,
sets CPU reset state including guest PC 0xBFC00000, and installs numeric I/O,
DMA, MDEC and interrupt handler entries. The handler implementations and the
PS1 execution loop are not thereby implemented. VFPU reset rows are represented
as host data, not an Allegrex interpreter.

It uses the existing ME control C model before any callback is registered.
For that path the original returns without waiting, so the host need not invent
an ACK. The native codec and clock service boundaries remain headless adapters;
this does not constitute sound output or the ME worker's live integration.

The observations above describe the first reset pass. The current diagnostic
also completes a disabled-SPU callback and obtains ACK 1 then ACK 0 from the
native worker. It stops at graphics initialization +0x1B9C4. It does not execute
PS1 instructions. See `native_spu_disabled.md` for the current run and limits.

Follow-up: the diagnostic now reaches the registered ME worker through an
explicit native output adapter. `out/ffvi_run.M0dYHv/result/` stops at the
unimplemented sample callback at POPS offset zero, with ACK still zero.
See `native_me_worker.md` for the startup/output adaptations.

The first run is `out/ffvi_run.QcgrY1/result/`, stopping at +0x1A494 after
51 instrumented calls (including repeated calls), not 51 complete functions.
The normal-path regression `out/ffvi_run.qtndeA/result/` still stops at +0x28DF8.
Both keep `game_executed: false`. Validation is compilation, instruction review
of the new routines and the real host diagnostic run, not exhaustive equivalence.
