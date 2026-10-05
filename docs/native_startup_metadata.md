# Disc startup reaches UI initialization

The native FFVI run `out/ffvi_run.RhcH0p/result/` passes the former +0x3764C
blocker and returns from the supported +0x1B2F0 disc-init path. It then stops
at +0x28DF8. There is still no PS1 execution, rendering or sound.

`src/native/pops_metadata.c` implements the no-savedata-list branch of +0x3764C,
the in-header branches of +0x24CC8, halfword count +0x24CA0 and the no-auxiliary
payload branch of +0x1C590. FFVI exercises the empty optional-metadata and
no-auxiliary branches. Directory scanning and external payload loaders remain
explicit blockers. Normal compilation and the real diagnostic run were checked;
there is no new claim of exhaustive tests or binary equivalence.

CD event creation uses a headless host adapter. The event begins at zero;
the worker at +0xDA3C first waits for bit 1 (mode 0x21, call at +0xDA90).
Only that initial wait is queued. No CD sector request is executed by it yet.

A read-only Ghidra pass in `out/startup_metadata.xyfozI/` successfully exported
the previously timed-out +0x28DF8 function at the existing 60-second budget.
It includes 1,890 lines of raw pseudocode, mostly UI/savedata orchestration.
Earlier failed output and the saved database remain untouched. The new text
is reference material, not a reconstructed function.
