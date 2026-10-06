# Unicorn S330 transfers and configuration check

The installed engine is Unicorn 2.1.4. `generated_unicorn.c` selects MIPS32
little-endian and 24KF immediately after `uc_open`, before other engine APIs.
Both the Python probe and the linked C archive come from `.tools/verify-env`.

At the user's request, the configuration was checked rather than assuming the
exception required a workaround. `out/unicorn-config._ygjvw24/results.json`
contains 96 isolated runs: all 16 advertised MIPS32 models, default versus
CU2-enabled CP0 Status, and an integer control plus MFV/MTV S330. All 64 VFPU
cases raise exception 21. The integer control returns 42 on 15 models; I7200
does not execute that normal-MIPS control as expected and is not an alternative
configuration for this image. For 24KF the tested status words are 0x20400004
and 0x60400004. Enabling CU2 does not implement Allegrex's VFPU transfers.

The local reproducer also confirms that a code hook can stop before a scalar
transfer outside a delay slot, avoiding the exception state entirely.
The adapter now intercepts only exact MFV/MTV S330 encodings in generated
cache pages, returns from Unicorn, transfers the raw bits and resumes at the
next instruction. Generated words are not patched. Original PRX pages remain
nonexecutable. Other VFPU instructions are not covered by this small bridge.
Transfers in branch delay slots stop explicitly rather than lose the branch.

The existing Unicorn fixture exercises both code caches, both transfer
directions, nonnumeric bit patterns, byte preservation and explicit rejection
of the unsupported delay-slot case. GTE/emitter tests continue to pass.

## Integrated result

`out/gte-s330.XYmeKl/result/` records one RTPT projection, one MFV S330 read
at generated address 156530120 (raw flags 4096), one NCLIP result (77) and one
AVSZ3 result (1394). It then reaches the next GTE command, 0x13, during another
block's compilation. Counts: 39,470,320 generated-cache observations and
40,359 entry transfers. `game_executed` is false; no framebuffer is rendered.

The final diagnostic-label refinement for unsupported delay slots was checked
by the focused fixture after that run; it does not change the reached path.
This is execution-adapter work, not another reconstructed original function.
