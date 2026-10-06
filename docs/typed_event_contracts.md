# Applied event and timer contracts

This pass applies four pointer/prototype contracts to a fresh Ghidra import.
It does not modify native emulator code or apply core state types over ambiguous
program-memory addresses. `ghidra/ApplyEventTypes.java` runs after
`ImportStateLayouts.java`.

## Confirmed contracts

| Entry | Body bytes | Prototype | Parameter storage |
| --- | ---: | --- | --- |
| +0x945C | 104 | `void pops_schedule_guest_event(PopsEvent *event, uint delay_cycles)` | a0:4, a1:4 |
| +0x9668 | 68 | `void pops_remove_guest_event(PopsEvent *event)` | a0:4 |
| +0x9B6C | 116 | `uint pops_synchronize_timer_counter(PopsTimer *timer)` | a0:4 |
| +0x9A54 | 124 | `void pops_schedule_timer_counter(PopsTimer *timer)` | a0:4 |

The pointer arguments explicitly have four-byte width in Ghidra. The event's
link/callback words remain 32-bit guest address types. These are analysis
contracts, not a request to cast the native harness's backing memory to host
pointers.

The original listing supports the parameter and return locations. In particular
+0x9B88 places the current timer count in v0, and +0x9A90 consumes that value
after the call at +0x9A60. The old decompilation displayed +0x9B6C as void,
losing the useful return contract. The new prototype preserves it.

## Visible result

The typed export now names the event's `next`, `prev` and `deadline_cycles`
fields, and the timer's `origin_cycles`, `target_with_flags`, `mode_with_status`
and `clock_shift`. The call from timer scheduling is rendered with the embedded
node, `pops_schedule_guest_event(&timer->event, ...)`, and synchronization is
rendered as a value-returning call with the timer argument.

All four functions decompiled successfully. Checks verify body extents, a0/a1
parameter storage, named field accesses, the synchronization return and the
embedded-event call. The export still contains the warning about an unknown
calling convention with locked parameter storage. The GP-relative globals also
remain generic GP labels; their actual runtime backing is not established merely
by typing the argument. Those warnings and limitations are preserved, not hidden.

## Outputs

- New project: `/tmp/repops-event-types.vZ1CTi/repops_event_types.gpr`.
- Log: `out/ghidra-event-types.wt7Pth/headless.log`.
- Prototype/storage report: `out/ghidra-event-types.wt7Pth/contracts/contracts.json`.
- Four typed C listings: the same `contracts/` directory.
- Recovered type archive: `out/ghidra-event-types.wt7Pth/types/repops_types.gdt`.

The C exports are analysis output, not source implementations and not compilation
or equivalence results. They stay outside Git. No old project was overwritten.
The project path is temporary; the scripts and ignored exports allow the result
to be recreated.

## Reproduce

From the repository root, with the pinned local decrypted ELF:

```sh
ROOT=$(pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
PROJECT=$(mktemp -d /tmp/repops-event-types.XXXXXX)
RESULTS=$(mktemp -d "$ROOT/out/ghidra-event-types.XXXXXX")
printf '%s\n' "$PROJECT" > "$RESULTS/project_path.txt"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" repops_event_types \
  -import "$ROOT/build/pops_660.prx.dec" -noanalysis \
  -scriptPath "$ROOT/ghidra" \
  -postScript ImportStateLayouts.java "$ROOT/data/state_layout.json" "$RESULTS/types" \
  -postScript ApplyEventTypes.java "$RESULTS/contracts" \
  > "$RESULTS/headless.log" 2>&1
test -s "$RESULTS/contracts/contracts.json"
```

Check `completed` and `parameter_storage` in the report rather than assuming
success from headless's exit code. The script refuses an existing export path.
Type/prototype improvements do not increase the count of complete original
function bodies in `data/function_progress.csv`; runtime behavior is unchanged.
