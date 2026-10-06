# Advancing the cooperative ME during SPU polling

After the idle postmix started returning samples, the next integrated diagnostic
stalled while polling SPUSTAT after a SPUCNT=C000 write. The trace contained
1,740 reads after that write and no further ME samples: this was host-adapter
starvation, not a justified device-ready value from the original code.

## Host scheduling adjustment

`pops_me.c:rp_pops_me_service_due` now allows one cooperative worker step at a
SPU read boundary when at least 0x300 guest cycles have elapsed since the last
produced sample. The worker still invokes the actual reconstructed callback and
its status writes; the reader is not patched to fabricate a ready value.

The clock uses `rp_core_guest_cycles(c)`, based on the named core deadline and
downcount fields in `pops_state.h`. The dispatch bridge saves/restores that
existing downcount around the service. The original SPU register reader remains
a separate reconstructed function. Last-sample time belongs to the host context,
not an invented original firmware field.

The 0x300-cycle quantum follows the existing POPS audio pacing relationship in
+0x11520 and its SPU write pacing path. This is lazy scheduling for the current
harness, not a recovered global ME scheduler or an exact hardware concurrency
model: it does not synthesize every missed sample between sparse read boundaries.
A call to the worker need not produce a sample immediately because the worker
has output/control phases. Do not count host service steps as audio samples.

## Completed integrated run

`out/postmix-vertical.i4gao4/result/run.json` records a completed diagnostic:

- Next boundary: `ME_enabled_reverb_write_path_not_reconstructed`, +0x5AC.
- 26,736,885 generated-cache instruction hook observations.
- 2,615 compiled-entry transfers.
- 190,376 reconstructed-function call entries and 1,442 host service calls.
- 1,056 completed active callback samples, all zero in the reached startup path.
- 2,153 due-at-read worker steps.
- `game_executed: false`; no rendered frame, audible music or successful FFVI
  boot has been demonstrated.

SPUSTAT polling is passed, followed by initialization transfers and a later
SPUCNT=C080 write. Reverb is then enabled and the new real boundary is reached.
The earlier runs `ffvi_run.QdSCTx` and `ffvi_run.8QivN0` were killed by the
wrapper's 30-second host limit; their incomplete traces are not successful run
reports. The latter already progressed beyond the old polling wait.

## Reproduction and checks

The Python runner accepts `--timeout`; `run_ffvi.sh` forwards `REPOPS_TIMEOUT`.
This only changes the host command's limit, not guest cycle calculations.
The completed diagnostic used 120 seconds as its cap, rather than the 30-second
default:

```sh
REPOPS_DIAGNOSTIC_SKIP_UI=1 REPOPS_TIMEOUT=120 ./run_ffvi.sh
```

Use a fresh result directory when invoking `scripts/run_native.py` directly.
The underlying native executable returns 78 at a recorded reconstruction
boundary; the wrapper returns zero when a valid diagnostic was captured, not
when the game boots. A host timeout is still an error, never a device-ready or
emulator-success result.

The native build and focused SPU, ME worker, event scheduler and Unicorn cache
tests pass. New clock-field checks cover the named addresses and wrapping
subtraction. No claim of exhaustive timing or original-binary equivalence is
made. The remaining work includes enabled reverb writes, streaming CD audio,
other envelope/sweep cases and broader device/rendering behavior.
