# Recovered state types in Ghidra

`ghidra/ImportStateLayouts.java` imports `data/state_layout.json` into the
current program's datatype manager and exports a reusable `repops_types.gdt`.
The script checks the pinned POPS hash, base-zero import and Allegrex language.
Run it only in a fresh analysis project, not over an existing working database.

## Successful run

- Project: `/tmp/repops-state.E08G6e/repops_state.gpr`.
- Log: `out/ghidra-state.GMXnwg/headless.log`.
- Archive: `out/ghidra-state.GMXnwg/export/repops_types.gdt`.
- Exported field metadata: `out/ghidra-state.GMXnwg/export/types.json`.
- Datatype category: `/RePops/Recovered`.

The generated archive remains in the ignored output directory even if the
operating system later removes the temporary project directory. Re-import the
pinned image and rerun the script to reconstruct the project. Open the GDT using
Ghidra's Data Type Manager when working on another analysis copy.

The successful run used the locally installed Ghidra 12.1 and Allegrex extension.
It created eleven root layouts: the ten schema-defined types plus
`CoreStatePartial` (0x4000 bytes). Nested helper structures and unions also exist
in the datatype manager and archive.

## What was checked

The export was inspected for exact sizes: alternate prefix 0xB04, event 0x10,
frame phase 0x14, timer 0x20, I/O pair 8, shared voice registers 16, volume sweep
10, envelope 16, mixer voice 0x74, instruction record 16 and core partial view
0x4000. The nested frame callback resolves to core+0x35F0. The mixer decoded
array is 56 bytes at +0x3A and its stopped byte remains at +0x72.

The instruction record has two explicit four-byte unions: category/cost versus
emitted entry at +4, and source fields versus patch-site address at +12. This
preserves the phase-dependent representation instead of pretending both views
are simultaneously valid.

## What is intentionally not applied

The script adds types and three explanatory code comments. It does not create
program data at runtime state addresses, set a global GP value, reanalyze the
whole program or migrate native C to struct casts. The image's code at numeric
module offsets 0x10000..0x13FFF is not the core scratchpad at those same runtime
addresses. Guest code/data addresses remain u32 typedefs, not host pointers.

Comments identify the ME bases at +0, the incoming-GP initializer at +0x1C254
and the event contract at +0x945C. Type presence in the datatype manager does
not mean that every decompiled variable is now correctly typed. The next step
is context-specific application to functions/overlays after their bases and
lifecycle are established. No new emulator behavior was added in this pass.

## Reproduce in a fresh project

From the repository root:

```sh
ROOT=$(pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
PROJECT=$(mktemp -d /tmp/repops-state.XXXXXX)
RESULTS=$(mktemp -d "$ROOT/out/ghidra-state.XXXXXX")
printf '%s\n' "$PROJECT" > "$RESULTS/project_path.txt"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" repops_state \
  -import "$ROOT/build/pops_660.prx.dec" -noanalysis \
  -scriptPath "$ROOT/ghidra" \
  -postScript ImportStateLayouts.java \
    "$ROOT/data/state_layout.json" "$RESULTS/export" \
  > "$RESULTS/headless.log" 2>&1
test -s "$RESULTS/export/types.json"
test -s "$RESULTS/export/repops_types.gdt"
```

The checks after headless matter: script errors do not always produce a failing
headless exit code. Output directories are new and the script refuses an
existing export directory. Ghidra 12.1 rejected a project path containing the
`.codexify` directory, so the successful project was created under `/tmp` rather
than inside the scratch workspace. That earlier failed attempt is recorded in
`out/ghidra-state.LaQQF9/headless.log`; it did not modify existing projects.
