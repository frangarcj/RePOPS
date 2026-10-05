# repops

Reconstructing the PSP 6.60 POPS-related modules in C and running that C
natively in a macOS harness. This is not a Vita port, a PSP PRX replacement
project, or simply running the original inside another emulator.

Original PRX files are analysis/data inputs; Ghidra supplies relocated data and
raw pseudocode. Reviewed C is compiled by the host compiler. JPCSP is a source
of reference semantics, not the execution engine. Firmware, games, bulk
decompiler output and generated executables are excluded from Git.

## Run FFVI on the native harness

```sh
./run_ffvi.sh
# An explicit PBP path is also supported, including spaces:
./run_ffvi.sh "$HOME/Downloads/Final Fantasy VI.PBP"
```

The root script compiles the native C, prepares a hash-checked Ghidra data
image on its first run, and creates a fresh `out/ffvi_run.*/result/` directory.
It uses `input/games/final_fantasy_vi/EBOOT.PBP`, falling back to the explicit
Downloads filename above. The original game and firmware are never modified.

**FFVI does not run yet.** The tested path reaches module startup, syscall-stub
preparation, the real PBP icon (80 by 80), the 736,384-byte extended PSISOIMG
header and normalized ID `SCES03828`. It applies the real game-configuration
row, prepares 7,141 disc-block entries and savedata metadata, then stops at
startup UI `+0x28DF8`. The current trace records 21 native function entries,
several of which execute only a reviewed prefix. This is not a count of 21
completely reconstructed functions. The provider path is an explicit
single-disc, unprotected-format adapter; protected-file services are not
silently treated as successful. See [the header notes](docs/native_disc_header.md).

`trace.jsonl` records milestones and the precise blocker; `run.json` records
provenance and `game_executed: false`. The wrapper exits zero when it captures
a valid diagnostic run; the underlying native executable exits 78 at a blocker.
Neither status claims that a game booted.

Requirements: a C11 compiler, Python dependencies from `requirements.txt`,
`pkg-config`, `libpng`, and Ghidra with the Allegrex extension for initial data
export. The native executable does not require Unicorn, PSP GCC or Java at
runtime once its data image has been prepared. Configuration variables:
`REPOPS_PYTHON`, `REPOPS_GAME_PBP`, `REPOPS_NATIVE_IMAGE`, `REPOPS_POPS_ELF`, and
`GHIDRA_HOME`.

See [the native harness notes](docs/native_harness.md) for adaptations,
remaining limitations, firmware provenance, and reproduction commands.

## Function progress

```sh
python3 scripts/function_progress.py
# Compare the register with a particular native run:
python3 scripts/function_progress.py --trace out/ffvi_run.c93OG9/result/trace.jsonl
```

`data/function_progress.csv` counts original functions once per input profile
and entry. A complete body can still call unimplemented host services; its
validation scope and integration status remain separate. Partial paths and
format adapters are not promoted to complete by being called successfully.
The register covers audited work and named next targets, not every undiscovered
function in the firmware. Decompiler output and call counts are not progress
percentages.

## Media Engine reverse

The Media Engine is an explicit reconstruction workstream, not just a set of
startup stubs. The target includes the POPS callback at module offset zero,
the provider's ME loop, sample production and shared-memory communication.
The existing registration/startup models remain useful but do not constitute
a recovered mixer. See [the ME reverse plan](docs/media_engine_reverse.md).

The cooperative [native ME worker](docs/native_me_worker.md) is implemented
as an isolated model; the POPS sample callback and live integration are pending.

```sh
python3 scripts/audit_me_targets.py --out out/me_target_audit_new
```

This audits hash-pinned code windows and compares the current corpus provider
with the earlier ARK provider. It does not execute firmware or claim complete
function boundaries; all generated reports stay outside Git.

## Current target

- PSP firmware: 6.60
- module: `pops_01g.prx` / module name `pops`
- architecture: Allegrex / little-endian MIPS32
- known repacked PRX size: 521,564 bytes (not proof of Sony encryption)
- known decrypted ELF size: 1,134,970 bytes
- decrypted reference SHA-256: `6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0`

`popsman` selects and loads `pops_%02dg.prx`, then starts it with zero arguments.
Do not confuse a library name with its provider: only one import uses the
`scePopsMan` library name, but POPSMAN also exports `sceMeAudio`. Versioned
metadata attributes **30 POPS imports** to POPSMAN, not one.

## Earlier analysis and isolated-model results

`out/decompiled/pops_660/` contains one `.c` file for each of 695 functions in
the current Ghidra database. 693 decompiled; two files record a failure instead.
`index.json` contains calls, warnings, errors and provenance. This is **raw
pseudocode, not a compilable or behaviorally verified emulator**. The reference
pass reported 64 files with decompiler warnings and 13,903 applied relocations.

`src/bootstrap.c` is separate: a buildable host-call model of the 128-byte
`module_start` routine. Its 36 contract tests pass, but there has been no full
binary-equivalence test or PSP/Vita execution. `out/reviewed_bootstrap.c` shows
the Ghidra result after fixing the exit function's no-return contract.

The next pass analyzes POPSMAN itself: 160 Ghidra entries exported, 852 applied
relocations, and 30 consumer/provider services matched from the actual binary
tables. The ARK provider is **not established as pristine Sony firmware**;
its export names include `noAudio`/`noAudio_driver`. `sceMeAudio` includes
graphics, file and system services, not just sound. See
[`docs/popsman_contract.md`](docs/popsman_contract.md).

Three reviewed leaf models in `src/popsman_mailbox.c` passed **3,252
binary-vs-native-C cases** using Unicorn and the hash-pinned original bytes.
This checks bounded observable behavior, not PSP devices, concurrency or the
full emulator. Their separate 24 callback-contract cases pass with sanitizers.

The resumed ME pass adds `src/me_registration.c`: a reviewed model of the
132-byte `DE630CD2` registration routine. It passed 2,752 comparisons against
loader-relocated instructions across two native optimization levels, with
the startup helper explicitly stubbed. It also has 24 sanitizer-tested C
contract cases. This does **not** validate ME hardware startup. See
[`docs/media_engine_registration.md`](docs/media_engine_registration.md),
including the execution-hook limitation and preserved failed attempts.

## Layout

- `run_ffvi.sh` - build/run entry point for the macOS native C harness
- `src/native/` - checked guest memory, reconstructed startup prefixes, PNG adapter
- `scripts/prepare_native.sh` - fresh Ghidra data image, without bulk recompilation
- `scripts/run_native.py` - provenance checks, native build/run and diagnostic capture
- `scripts/index_prx_corpus.py` - firmware inventory and conservative dependency graph
- `scripts/analyze_pops.py` - ELF/PRX metadata, imports and Capstone function discovery
- `ghidra/ExportFunctions.java` - headless Ghidra function inventory
- `ghidra/ExportDecomp.java` - headless decompilation of selected functions
- `ghidra/ExportAllDecomp.java` - one C file per function, with JSON index
- `ghidra/ApplyBootstrapContracts.java` - evidence-backed bootstrap annotations
- `scripts/audit_contract.py` - match imports to a provider by library and NID
- `src/bootstrap.c` - reconstructed, unit-tested host-call model
- `src/popsman_mailbox.c` - three reviewed guest-bus callback models
- `src/me_registration.c` - callback/stack registration model; startup is a boundary
- `scripts/verify_me_registration.py` - bounded registration-vs-C tests, helper mocked
- `scripts/map_popsman.py` - real import/export join and static caller report
- `scripts/run_popsman_ghidra.sh` - fresh, isolated POPSMAN analysis/export
- `scripts/verify_mailbox.py` - original MIPS bytes vs native C, optional Unicorn
- `tests/` - synthetic parser tests and bootstrap contract tests
- `docs/interface.md` - POPSMAN <-> POPS boundary notes
- `docs/findings.md` - initial subsystem map and reverse-engineering notes
- `input/` - user-supplied encrypted PRX files (ignored)
- `build/` - decrypted/reference analysis files and Ghidra projects (ignored)
- `out/` - generated reports (ignored)

## Setup

```sh
python3 -m pip install -r requirements.txt
```

Ghidra needs an Allegrex-aware loader/language. On this macOS/Ghidra 12.1
setup the pinned extension can be installed with:

```sh
scripts/install_ghidra_allegrex.sh
```

The script verifies the release ZIP SHA-256 before installing it into the
per-user Ghidra extension directory; it will not replace an existing extension.

Run the Capstone inventory:

```sh
python3 scripts/analyze_pops.py build/pops_660.prx.dec \
  --reference-asm build/pops_660_reference.s \
  --out out
```

Run Ghidra headless:

```sh
scripts/run_ghidra.sh build/pops_660.prx.dec
```

Each import uses a fresh temporary project directory to preserve manual
annotations in earlier projects. The selected path is saved in
`out/ghidra_project_path.txt`. To export the analyzed project as C:

```sh
sh scripts/export_c.sh out/decompiled/new_pass
```

The output directory must be new or empty. Export opens Ghidra read-only and
applies the bootstrap annotations in memory; it does not change the saved
project. Keep the earlier exports for comparison.

Compare Capstone/Ghidra inventories against an optional historical listing.
Ghidra is seeded with known symbols; agreement is not a proof of completeness:

```sh
python3 scripts/compare_maps.py --reference build/pops_660_reference.s
```

## Tests

```sh
python3 -m unittest discover -s tests -v
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address \
  src/bootstrap.c tests/test_bootstrap.c -o build/test_bootstrap
./build/test_bootstrap
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address \
  src/popsman_mailbox.c tests/test_popsman_mailbox.c -o build/test_popsman_mailbox
./build/test_popsman_mailbox
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address \
  src/me_registration.c tests/test_me_registration.c -o build/test_me_registration
./build/test_me_registration
```

The optional instruction-execution tests require the pinned local POPSMAN
reference and `unicorn==2.1.4`. They refuse wrong hashes and existing output
directories. Full reproduction commands and limitations are in
`docs/popsman_contract.md`; these are not part of the synthetic Python suite.

The parser measures 159 function imports in 23 libraries. Its
251,044-byte scan window is not a proven `.text` size; the ELF has no section
headers and executable segments also contain data.

## Binary policy

Keep copyrighted firmware modules outside Git. The scripts operate on a local decrypted ELF. A known hash is recorded so analysis can be reproduced against the same revision without redistributing it.
