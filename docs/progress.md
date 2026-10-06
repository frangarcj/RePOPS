# RePops progress

## Current: CD Setloc and SeekL deliver their responses

The native controller now handles banked registers, parameters, delayed
responses and IRQ acknowledgement. `out/cd-seek.grqSK4/result/` completes
Setloc for sector 4 and SeekL, including three response publications. It stops
at the next command, Setmode 0x0E with parameter 0x80. The worker request for
block zero is queued, not claimed as completed disk I/O.

This run records 38,182,482 generated-cache observations and 4,883 transfers.
The recovered audio delay requests 13,035 microseconds; the explicit host
adapter executes 574 ME samples rather than skipping the yield. PSP scheduling
granularity is not reproduced. `game_executed` remains false.

New C code uses the CD/controller/cache layouts in `pops_cdrom.h`. Native
static assertions and all 18 schema layout extents pass; CD, events, SPU, ME
and Unicorn smoke tests pass. See `cd_controller.md`.

The ledger has 73 complete and 63 partial POPS bodies. Exact matches against
the historical 536-entry internal inventory are 65 complete (12.1%) and 48
partial (21.1% combined). The counts describe modeled bodies at their recorded
verification scope, not game compatibility, hardware validation or time left.

## Earlier: signed reads reach the CD-ROM write boundary

LH +0x1DE8 and its specialized entrances now share the existing memory routing
with explicit sign extension and service width. Guarded LH/LHU RAM helpers
avoid repeated C crossings without allowing I/O addresses through the fast path.
The full POPS ledger now has 66 complete and 61 partial entries; LH is outside
the historical inventory, so the comparable 11.2%/19.8% figures do not change.

`out/half-fast-vertical.HU29bd/result/` stops at byte-store helper +0x1C70 when
attempting the CD-ROM port 0x1F801800. The existing handler table selects writer
+0xD1B0, not yet reconstructed. It records 38,128,915 generated-cache hook
observations and 3,203 transfers. No game boot is claimed.

The baseline and guarded runs match all 58,245 selected device/audio/event
effects and the final guest PC. Guarded reads remove 54,406 helper crossings
(41.88%), not a measured wall-time percentage. Native SPU/ME and Unicorn smoke
checks pass. See `cpu_signed_halfword.md` and its comparison artifact.

## Latest: enabled reverb returns to CPU execution

The +0x5AC boundary is recovered in the shared enabled/disabled reverb path.
It preserves IIR write-next cursors, ordered allpass writes/reads and the wet
voice input selected by capture phase. Existing C layouts are reused; no fresh
global type pass was needed. SPU and ME-worker smoke checks pass, including
nonzero wet input, aliased taps and cursor wrap for both phases.

`out/reverb-vertical.nvPi1C/result/` completes the integrated diagnostic at the
next CPU helper +0x1DE8 (signed halfword read): 26,738,259 generated-cache hook
observations and 2,643 transfers. This is not a game boot or an equivalence
proof. The ME callback remains partial; see `me_enabled_reverb.md`.

## Current: active ME samples and SPU polling progress

Vertical execution has resumed, interleaving each recovered function with the
types it exposes. New code must use those names in native C, not merely in
Ghidra. `pops_state.h` now supplies wire layouts and named clock/postmix field
accesses; the actual ME scheduling code calls `rp_core_guest_cycles(c)`.

The active callback completes its reached idle-CD, disabled-reverb-write and
fixed-master-volume path. A synthetic test returns packed sample 0x007C01F2;
the real startup run completes 1,056 active samples, all zero. It does not
complete the callback's other paths or demonstrate audible FFVI music.
See `me_idle_postmix.md`; the master and reverb layouts are also in the fresh
16-root Ghidra archive at `out/postmix-types.1kF9Iy/export/repops_types.gdt`.

The first run exposed ME starvation during SPUSTAT polling. A lazy cooperative
step at the SPU read boundary now advances the real callback instead of faking
the status value. `out/postmix-vertical.i4gao4/result/` records a complete run
through that wait and subsequent initialization: 26,736,885 cache instruction
observations, 2,615 transfers, next boundary enabled reverb writes +0x5AC.
`game_executed` remains false. See `me_poll_scheduling.md` for scope and the
120-second host cap used by this run; earlier 30-second timeouts are not success.

Native SPU, ME-worker, event and Unicorn tests pass. The callback remains one
partial function: the comparable coverage stays 11.2% closed / 19.8% including
partials, not a measure of hours or game compatibility.

## Earlier structural workstream

The requested first pass is in `pops_state_layout.md` and
`../data/state_layout.json`. It distinguishes main scratchpad GP, shared-ME GP,
the private mixer base and an alternate GP context whose ownership remains open.
Twelve descriptive type layouts cover events, frame phases, timers, I/O pairs,
shared voice registers, volume/envelope state, mixer voices, compiler records
and the separate alternate-GP, ME-shared and ME-private prefixes.
Their field widths/extents were checked; that is not complete behavioral recovery.

The concrete `gp + 0x35F0` field is `frame_phase.event.callback`, inside the
event at GP+0x35E4. Important unresolved/overlapping cases include the sentinel
control word, phase-dependent compiler records and alternate-GP lifecycle.
`ImportStateLayouts.java` has now installed these types plus `CoreStatePartial`
in a new Ghidra project and exported a reusable GDT archive. Thirteen root sizes,
the nested frame callback and the instruction-record unions were checked.
No original module memory has been retyped and no mass C offset replacement
has been performed. The ME-specific pass adds typed synthetic state below.
See `ghidra_state_types.md`; pre-existing projects and emulator source are unchanged.

The next pass applies event/timer prototypes to four checked functions with
`ApplyEventTypes.java`. Ghidra now renders event fields, timer fields and the
embedded call `pops_schedule_guest_event(&timer->event, ...)`. The timer sync
helper at +0x9B6C now has its observed u32 return, correcting the old void
inference. All four exports completed with expected a0/a1 storage; unknown
calling-convention warnings and unresolved GP globals are retained.
See `typed_event_contracts.md`. No runtime features or offset refactor were added.

The optional `implicit-core` export now also resolves those GP labels to
`core->event_deadline`, `core->event_downcount` and the sentinel links. The
context is a custom-storage analysis parameter in gp:4, not a new ABI argument.
A decompiler-local CompilerSpec view removes only GP's spacebase/global rule;
the installed extension and existing project are unchanged. All four exports
completed and propagate core through the timer calls. See `implicit_gp_context.md`
and `out/ghidra-gp-view.uBbqaV/contracts/`; read-only processing discarded the
temporary prototype changes. Native execution and coverage counts are unchanged.

The ME callback now has named shared/private state in a fresh analysis copy:
`MeSharedPrefix` (0x2C0) and `MeMixerPrefix` (0x179C). Both shared addresses map
to one uninitialized backing; the mixer prefix is separate. All synthetic blocks
are non-executable and have no supplied initial values. The latest export is
`out/ghidra-me-types.EuhMEh/state/`, with the type archive in its sibling `types/`.

The first typed export reordered mailbox reads across cached/uncached clears.
Marking both shared blocks volatile in the analysis view preserves the checked
consumer begin/end and read-before-clear sequence. This is a scoped decompiler
annotation, not a claim about the original C keyword or full cache semantics.
See `typed_me_state.md`; the event GP view still passes after extraction into
`RePopsDecompilerView.java`. No native SPU implementation changed in this pass.

The alternate initializer +0x1C254 uses incoming GP despite the raw decompiler
displaying 0x10000. Its caller sets GP=0x09FF8000. A 0xB04-byte cleared prefix,
distinct list links and a nibble-mask table are now documented without assigning
an unproven subsystem or assuming that the prefix is the complete allocation.

`reverse_coverage.md` records the measured percentage: 60 of 536 historical
internal inventory entries have complete reconstructed bodies (11.2%); another
46 are partial (19.8% combined). Twenty additional ledger starts are outside
that inventory and counted separately. This is not remaining-time or game-boot
completion. The full ledger still reports 66 complete and 60 partial POPS entries.

## Current: the active ME callback reaches the end of its voice loop

`out/ffvi_run.ZeJEMV/result/` reaches `ME_post_voice_mix_not_reconstructed`
at +0x288. The reconstructed callback processes release envelopes, block
flags/history, ADPCM decoding, interpolation, fixed volumes and capture writes
in the original 24-voice order. This invocation's voices are released and their
contributions are zero. A separate nonzero interpolation fixture produces the
expected capture value, rather than testing only cleared sample data.

Final CD/reverb/output processing is not reconstructed and no packed active
sample returns. Other envelope transitions, volume sweeps and ADPCM IRQ-overlap
paths still stop explicitly. The callback remains one partial original function;
processing 24 voices does not add 24 completed functions. CPU observations remain
9,580,458 with 1,688 transfers because this increment is native ME work.
`make native test-native-spu test-native-me` passes. See `me_voice_loop.md`.

Per the latest instruction, stop expanding functionality after this increment
and prioritize recovering/naming the state structures.

## Earlier: RAM initialization reaches the active ME callback

88ed77a recovers shift pairs, HI/LO records and exception emission. 81790b0
connects guarded memory fast paths and IRQ/DMA control, fixing a pending SH/LBU
adapter mix-up and the IRQ polling debit. faf316e adds timer mode/target
rescheduling, the RAM exception-vector compiler path and actual SPU status reads.

The next enabled-ME prefix consumes pending key/dirty masks, updates its IRQ
cursor and noise state, and initializes the first voice's control/release state.
It stops inside the callback at +0x11CC instead of fabricating an output sample.
`out/ffvi_run.vVQcDk/result/` records 9,580,458 generated-cache observations,
1,688 entry transfers and `ME_voice_sample_path_not_reconstructed`.

The run uses the explicit PSP UI bypass. Normal startup still stops at +0x28DF8.
There is no FFVI boot, rendered framebuffer or active audio. The generated-cache
counters exclude fast-helper instructions and are not a firmware coverage metric.
Focused emitter/event/SPU/Unicorn tests pass; these do not prove whole-emulator
equivalence. See `cpu_exception_and_hilo.md`, `cpu_memory_and_control.md`,
`timers_and_spu_reads.md` and `me_active_prefix.md` for scope and evidence.

## Earlier: RAM-derived blocks execute and program interrupts

cb2fa59 adds the RAM branch of +0x58C0, code-guard emission, publication and
execution of the second generated cache through the existing Unicorn engine.
The game selects RAM mode 0x8003, whose initial-prologue rules differ from
mode 3. Guest RAM and original PRX pages stay nonexecutable.

01e0a29 adds generic SLL/SRL/SRA emission and the +0x98C4 interrupt-register
writer. The initialization path now moves between RAM and BIOS-derived
blocks, resets additional SPU registers and writes interrupt status/mask.

`out/ffvi_run.LDgfH0/result/` reaches +0x2468, an unsigned-byte helper not yet
reconstructed, with 169,448 instruction-hook observations and 94 block-entry
transfers. No FFVI boot, rendered image or active voice mixing is claimed.
The cache, emitter and event smoke tests pass; the cache test now includes
a RAM-to-BIOS-generated-code transfer. See `ram_compiler.md` and
`ram_boot_followup.md` for unverified/unsupported paths.

## Earlier: BIOS requests execution from RAM

ce13509 connects expansion/BIOS byte reads and their call-site specialization.
It also corrects default-read width values and adapts the original 24-bit
SWL code-link patch to our base-zero helper addresses. Without that address
adaptation a reused block could jump to 01B8071C instead of 09B8071C.

0faef46 adds the reached nonhazard load-delay-slot controller path, byte RAM
stores, word reads, COP state reads and RFE emission. The ROM-resident BIOS
then reaches its program-copy loop and attempts to dispatch to A0000500.

`out/ffvi_run.AaJVia/result/` records 146,695 generated-code hook observations,
34 block-entry transfers and `non_BIOS_compiler_controller_not_reconstructed`
at +0x58C0. No RAM code or game has executed yet. This is an integrated
execution trace, not a separate byte-equality audit of the copied program.
Focused emitter, Unicorn-cache and event tests pass; see `cpu_byte_reads.md`
and `bios_ram_handoff.md`. The next workstream is the original RAM compiler
prologue, publication/invalidation and generated-code cache execution range.

## Earlier: guest events, SPU writes and BIOS function calls

The resumed pass starts at bf8ac95 and adds three executable increments:

- 391388c: +0x953C guest event dispatch and the initial video-phase callback.
  The cycle handoff returns to the BIOS rather than skipping expired events.
- 7603384: +0x7F00 SPU register writes, FIFO/key/dirty state and event requests.
  The ME shares their backing memory; the active mixer is still incomplete.
- 3dda1ac: cached stack accesses, internal links, constant branches and
  indirect returns in the original POPS compilation path.

`out/ffvi_run.kujVtA/result/` contains the integrated continuation. It reaches
the byte-read helper +0x1A90 after 29,339 generated-code hook observations and
11 compiled-entry transfers, including the BFC06EC4/01A60/03990 call chain.
The last saved guest block PC is BFC06ED4, not a per-instruction trace.
The normal startup still requires the PSP UI at +0x28DF8. There is no FFVI
gameplay, rendered framebuffer or active-voice audio.

Focused scheduler, SPU writer, disabled-SPU, emitter and Unicorn-cache checks
pass. These are contract/smoke tests plus a host run, not an assertion of
full firmware equivalence. See `guest_event_dispatch.md`,
`spu_register_writer.md` and `cpu_stack_and_returns.md`.

## Earlier executor change: Unicorn instead of a handwritten interpreter

The native build now links Unicorn 2.1.4 through its C API. The reconstructed
POPS compiler still emits its own cache, while the existing MIPS engine runs
it. Original PRX pages remain nonexecutable. Known helper PCs use explicit
engine exits and continue in reconstructed C; no native helper runs inside a
Unicorn hook. The old handwritten adapter is retained but not linked.

`out/ffvi_run.mbYN9U/result/` reproduces the existing BIOS path to BFC0039C
and the +0x3A90 compiler boundary. It records 731 instruction-hook events and
five compiled-entry transfers, not a new retirement or equivalence proof.
The focused `make test-unicorn-cache` check covers helper returns, the delay
slot, shared memory, FPR bits and native changes to translated code. This
removes an infrastructure burden, not a new set of reconstructed POPS functions.
See `unicorn_execution.md` for the MIPS32-versus-Allegrex limitations.

## Earlier in this pass: executing the C compiler's BIOS output

The volatile memory-card worker now produces the startup signal at +0x14CC64.
The reconstructed dispatch entry resolves its compiled target and a small
provisional interpreter executes **only the C-generated Allegrex cache**.
Calls into original POPS offsets use reconstructed C; unsupported targets
stop rather than falling back to original-firmware emulation. This is not an
ARM64 JIT or an optimized backend.

`out/ffvi_run.Rus1UO/result/` reaches BIOS PC BFC0039C after 731 emitted
instructions and five cache-entry transfers. The two initial conditional
loops finish, with branch conditions captured before a modifying delay slot.
The next compiler boundary is dynamic memory-base emission at +0x3A90.
The normal, non-diagnostic path still stops at PSP UI +0x28DF8.

Semantic commits in this pass: 49bea93 (card handoff), a05c789 (provisional
execution bridge), 3ec1c60 (conditional branches and deadline helper), and
5c295d3 (COP0 state writes). Focused card/emitter/bridge tests pass with
sanitizers. No game boot, enabled audio, renderer, complete CPU semantics or
new full binary-equivalence result is claimed. See `native_memory_card.md`,
`generated_code_execution.md`, `cpu_conditional_flow.md` and `cpu_state_writes.md`.

The sections below are preserved earlier checkpoints; their stop addresses
and statements about execution apply to those earlier runs.

## Earlier CPU reverse: first reconstructed analysis stage

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

## Earlier: native reset compiles the initial BIOS block

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
