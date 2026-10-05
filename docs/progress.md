# RePops progress

## CPU reverse: first reconstructed analysis stage

The +0x05154 analysis body is now C with its original 16-byte records and
recursive branch discovery. A focused first-BIOS comparison matches the
record buffer and scratchpad against the original routine. This is analysis,
not PS1 execution. The +0x058C0 setup and cost-accounting prefix now also matches
the same sample up to +0x5D5C, before instruction emission. The next focused
probe reconstructs register helpers and the immediate category of +0x6914,
producing the same first three Allegrex words with matching compiler state.
The known-base memory category now also matches: the first two BIOS stores
produce a 24-byte Allegrex prefix with identical records and scratchpad.
Dynamic addresses and other emitter categories remain pending. The initial
BIOS controller/link path was added below. See `cpu_memory_emission.md` and
`cpu_register_emission.md` for the narrow validation scope.
The follow-up flow probe reaches the end of the initial record region and
matches 348 emitted bytes, including the CPU-status write and exit sequence.
See `cpu_flow_emission.md`. The BIOS controller's actual record walk now also
matches 352 emitted bytes, records and scratchpad through +0x64F7. The final
BIOS link/cache-publication pass now matches too, including the 2.5 MiB cache
tables and returned entry. SV.Q clearing uses an explicit reset-row adapter
in the comparison. See `cpu_block_controller.md`; emitted code is not executed.
These functions are now linked into the integrated native diagnostic too.
The first reset's +0x94C4 exception-vector setup calls the reconstructed
compiler and publishes its 352-byte block at 0x09B80000. It is not executed.

## Current: native reset compiles the initial BIOS block

The normal FFVI startup path now applies the game configuration, prepares the
disc index and savedata metadata, and stops at PSP UI +0x28DF8. An explicit
`REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh` path bypasses that UI (not counted
as reconstructed) to investigate core initialization.

The diagnostic initializes CPU/device/SPU state, executes the disabled-SPU
callback in C, and obtains the start/resume ACKs from the native ME worker.
It also builds the graphics tables, captures the initial GE state lists and
inserts the first guest refresh event. The disabled-display path now returns,
GPU handlers are registered, RAM is cleared, and the actual reset calls the
native compiler. `out/ffvi_run.VzSqXd/result/` reaches the wait at +0x1A908;
the missing producer signal is not replaced with success. See
`disabled_display_refresh.md` and `cpu_block_controller.md`. Enabled mixing, the full UI,
renderer and PS1 instruction execution remain incomplete. No sound or gameplay
is claimed. See `native_spu_disabled.md` for the successful optimized and
sanitized runs and `data/function_progress.csv` for per-function scope.

Work is saved in semantic commits as each executable increment is checked.
The measurements below are earlier checkpoints, not the current stop address.

## Earlier: reconstructed C running on Mac with the user's FFVI PBP

The active goal is native C execution on macOS, not a Vita port, a matching
decompilation, or a replacement PSP PRX. See `native_harness.md` and run
`./run_ffvi.sh` from the repository root.

The native path now opens the real FFVI PBP, parses its 80-by-80 PNG icon,
selects disc zero, loads the 736,384-byte extended header and validates
`SCES03828`. It stops explicitly at per-game configuration +0x24770.
There is no game CPU execution, framebuffer or audio output yet. The 15
instrumented function entries include partial reconstructions. See
`native_disc_header.md` for format restrictions and the current traces.

Media Engine reconstruction is now explicitly in AGENTS.md and has a separate
workstream in `media_engine_reverse.md`, including the sample producer, ME
loop and shared-memory protocol. The nine-window audit does not mean the
mixer has been implemented. `me_wrapper.prx` is a candidate dependency whose
reachability still needs to be established.

The complete archived 6.60 firmware corpus was extracted: all 288 PRX parse.
The 114-module static dependency superset is not an actual load order; it
retains provider ambiguities and unresolved dependencies. The corpus POPS
matches the earlier input hash; the corpus POPSMAN differs from the old ARK
reference. Keep the old experiments and their results scoped to their hashes.

Validated in this continuation:

- Native C executable is Mach-O arm64; FFVI reaches +0x24770 under ASan/UBSan,
  without modifying the game or firmware. The new header/ID C tests also pass
  with sanitizers; these are not instruction-equivalence tests.
- A scratchpad/module-address collision was fixed and regression-tested.
- Fresh Ghidra data export reproduces SHA-256
  `7e3fe7f349a9f45464708b564c67f1dd1c387fbe05ec898c8d82b60a074cac65`.
- 41 Python tests are collected: 40 pass in the default environment and one
  optional Unicorn test is skipped. The earlier 30-test suite also passed in
  the verifier venv; the expanded suite was not rerun there in this pass.
- Earlier ME startup/control comparisons passed 6,488 scripted-device cases,
  including registration integration, and four deliberate negative controls
  were rejected. These are not actual PSP hardware tests.
- The PSP hybrid experiment passed 4,109 original-vs-compiled MIPS cases; it
  is preserved research, not the active deliverable.

Source work has been saved in incremental semantic commits. Game, firmware,
raw C and generated reports remain ignored and uncommitted.

## Historical checkpoints below

The following sections describe earlier phases. Their counts and pending work
are historical rather than a current project-completion estimate.

## Latest continuation: Media Engine registration

See `media_engine_registration.md`. Existing hashes and baselines were
rechecked, and Ghidra reproduced the relocated provider bytes read-only.
The new registration model passes 24 C contract cases and 2,752 bounded
instruction-vs-C comparisons at -O0/-O2, with the hardware helper mocked.
The harness records a repeated pre-instruction-hook limitation; this is not
hardware equivalence. Six synthetic harness tests join the prior 14 tests.
Existing files and failed runs remain intact. No emulator boot has occurred.

Latest passing reports: `out/media_engine_resume.9RXA5b/verification_final/`
and `out/media_engine_resume.9RXA5b/mailbox_regression/`. Final regression
results were 20 Python tests, 84 C sanitizer-tested contract cases, the prior
3,252 leaf comparisons and the new 2,752 registration comparisons. The latter
mock startup and account explicitly for repeated pre-instruction hooks.

## Follow-up: actual POPSMAN provider and bounded binary validation

The later phase is recorded in `popsman_contract.md`. It adds 160 exported
provider function entries, a raw-table confirmation of the 30-service contract,
three C leaf models with 3,252 passing instruction-execution comparisons, and
24 additional sanitizer-tested callback cases. The Python suite now has 14
synthetic tests. The material below records the earlier bootstrap checkpoint;
its counts and unperformed checks refer to that earlier phase.

## Actual deliverables

- `out/decompiled/pops_660/`: 695 per-function `.c` files from the existing
  Ghidra database, plus `index.json` and a warning README.
- 693 functions decompiled successfully. Two `.c` files explicitly report
  failure rather than pretending to contain a valid implementation.
- 64 successful outputs contain Ghidra warnings. Success means the decompiler
  produced text, not that the function is equivalent to the original binary.
- `out/reviewed_bootstrap.c`: separately re-decompiled after applying the
  no-return exit contract and checking bootstrap function boundaries.
- `src/bootstrap.c` / `src/bootstrap.h`: a compilable host-call model of
  `module_start`, not an emulator or a PSP/Vita port.

All generated bulk C stays under ignored `out/`. No original firmware binary
has been staged or published by this work.

## Reproducible measurements

| Measurement | Result |
| --- | ---: |
| Target ELF bytes | 1,134,970 |
| Candidate scan window bytes | 251,044 |
| Capstone candidate entries | 721 |
| Historical listing entries matched by Capstone | 656 / 656 |
| Ghidra database function entries in the export | 695 |
| Loader relocation records marked APPLIED | 13,903 |
| Measured function imports | 159 |
| Measured import libraries | 23 |
| Imports attributable to POPSMAN by metadata | 30 |

Input SHA-256:
`6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0`.

The Ghidra language was `Allegrex:LE:32:default`, image base zero. The
candidate scan window is not a measured `.text` section: section headers are
absent, and loaded executable regions can contain data. A match to the old
listing does not establish completeness or that every entry is a function.

## Two explicit decompilation failures

- `0x000130BC`: `Address Overflow in subtract: NO ADDRESS - 0x2`.
- `0x00028DF8`: decompiler timeout at the export's 30-second per-function limit.

Neither has been substituted with guessed C. The complete errors are retained
in the JSON index. Decompiler budgets can be raised or the analysis refined
for a subsequent export into a different directory.

## Bugs and incorrect inferences corrected

1. The import parser required 24 bytes, although a descriptor can be 20 bytes.
   It silently missed the last `UtilsForUser` descriptor and five imports.
   The fixed parser requires exact table consumption and bounds-checks reads.
2. One import from library `scePopsMan` does not mean one dependency on module
   POPSMAN. That module also exports `sceMeAudio`: 29 more imported functions
   match the versioned metadata. This attribution has not been live-traced.
3. Ghidra incorrectly followed a supposedly returning exit call into the next
   routine. Marking that service no-return produces a small, coherent
   `module_start` again. The checked machine-code range is `0x16000..0x1607F`.
4. A consumer of 16-bit words at `0x1E20C` is not automatically a PS1 IR backend.
   Its producer and role remain unknown. Do not design the port around that
   earlier hypothesis until they are established.

## Validation performed

- Nine synthetic Python inventory tests pass without any Sony binary.
- The C bootstrap model compiles as C11 with `-Wall -Wextra -Werror`.
- 36 callback-contract cases pass with AddressSanitizer and UBSan enabled.
- Original bootstrap instructions were checked using the bounded Capstone
  range tool. Its branches and constants agree with the reviewed Ghidra output.

Not performed: PSP execution, Vita execution, instruction-by-instruction
binary equivalence, testing of the full emulator, or a build of the bulk C.

## Continuing the work

Use `sh scripts/export_c.sh out/decompiled/next_pass` for a fresh read-only
export. Existing directories are refused so earlier results are not lost.
The saved Ghidra project location is in `out/ghidra_project_path.txt`.
Next substantive reverse task: recover the POPSMAN/Media Engine contract,
while treating the 16-bit executor's identity as unresolved.
