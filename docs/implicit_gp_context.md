# Typed implicit GP context in the decompiler

The optional `implicit-core` mode of `ghidra/ApplyEventTypes.java` makes the
incoming GP register visible as `CoreStatePartial *core` for the four checked
event/timer helpers. This is an analysis signature, NOT an extra ABI argument,
a change to firmware instructions or a new native emulator implementation.

## Why an ordinary type annotation was insufficient

The installed Allegrex compiler specification declares:

```
<spacebase name="gp" register="gp" space="ram"/>
```

and marks that space as global. A first read-only probe added a custom-storage
parameter in register GP, but the decompiler still produced `uGp...` globals.
The probe is retained at `out/ghidra-gp-probe.b1XRnw/contracts/`; its
`typed_core_access` fields are false. Merely seeing the core parameter in a
prototype is therefore not evidence that state accesses have been typed.

## Local analysis view

In `implicit-core` mode, the script encodes the current CompilerSpec and removes
only the GP spacebase declaration and the matching global range. It parses
that into a separate BasicCompilerSpec, then gives the decompiler a forwarding
Program view whose `getCompilerSpec()` returns that local object. Other Program
operations use the actual analysis copy. No installed language file, persisted
compiler selection or instruction bytes are replaced.

The checked functions retain a0/a1 argument storage. `core` uses gp:4 and is a
four-byte pointer to the recovered core layout. GP is already an input to these
original functions; the extra displayed parameter makes that implicit dependency
explicit for analysis. It is not evidence that Sony's original C declared a
third argument, and it must not be propagated to routines that switch GP to
another context.

The successful run processed the existing typed project with `-readOnly`, so
function-signature edits made during the probe were discarded after export.
The canonical project's ordinary prototypes remain unchanged. Opening that
project in the standard UI alone does not reproduce this specialized view;
run the script to obtain the context-aware export.

## Verified result

`out/ghidra-gp-view.uBbqaV/contracts/` contains four C exports and metadata.
The three helpers that directly access GP now name fields such as:

```
core->event_deadline - core->event_downcount
&core->event_head_next
```

The timer scheduler does not directly dereference GP; it passes the same core
argument to synchronization and event insertion. Its typed call uses
`&timer->event`. All four exports completed and preserve the expected parameter
locations, including `core:gp:4`. Unknown-calling-convention warnings remain.

An XML comparison confirms that the compiler view differs only by those two GP
entries. The exported `source_compiler.cspec` and `context_view.cspec` record the
actual inputs to this experiment. There are no generic `uGp...`/`iGp...` labels
in the four successful exports. This is a readability/type-propagation check,
not a formal proof of decompiler correctness or whole-emulator equivalence.

## Reproduce

First create the typed project described in `typed_event_contracts.md`, or use
the successful project while its temporary directory still exists:

```sh
ROOT=$(pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
RESULTS=$(mktemp -d "$ROOT/out/ghidra-gp-view.XXXXXX")
"$GHIDRA_HOME/support/analyzeHeadless" \
  /tmp/repops-event-types.vZ1CTi repops_event_types \
  -process pops_660.prx.dec -noanalysis -readOnly \
  -scriptPath "$ROOT/ghidra" \
  -postScript ApplyEventTypes.java "$RESULTS/contracts" implicit-core \
  > "$RESULTS/headless.log" 2>&1
test -s "$RESULTS/contracts/contracts.json"
```

The ordinary one-argument mode keeps the original compiler specification and
only applies the event/timer parameter types. The special mode is intentionally
limited to these confirmed core-context helpers. ME shared state and the separate
0x09FF8000 context require their own scoped views, not a global rename to core.

No native sources or function-completion ledger entries change in this pass.
