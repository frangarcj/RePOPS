# Applied ME shared and mixer state types

This structural pass applies two recovered ME prefixes to synthetic analysis
memory in a new Ghidra project. It adds no emulator functionality and does not
supply firmware initial values. `ghidra/ApplyMeStateTypes.java` runs after
`ImportStateLayouts.java` and uses the shared decompiler-only GP view from
`RePopsDecompilerView.java`.

## Two objects, three addresses

| Symbol | Address | Typed extent | Backing |
| --- | --- | ---: | --- |
| `me_shared_cached` | 0x09F40000 | 0x2C0 | Uninitialized shared prefix plus sample-address range |
| `me_shared_uncached` | 0x49F40000 | 0x2C0 | Byte-mapped alias of the same cached backing |
| `me_mixer` | 0x09FF0000 | 0x179C | Separate uninitialized private mixer prefix |

The shared analysis block spans 0x802C0 bytes: the 0x2C0-byte prefix followed
by the sample range selected through the 16-bit block address shifted by three.
This is a convenient analysis mapping, not a recovered allocator size. The
private extent matches the reset-cleared prefix. Unknown regions stay explicitly
unnamed; neither prefix is claimed as complete allocation/ownership recovery.

The cached and uncached symbols must not become independent native C objects.
Their bytes alias even though the original callback deliberately uses different
addresses for cache operations, reads and writes. All three blocks are writable,
non-executable and uninitialized. Probing their first byte produces no value;
no zero-filled state or fabricated register defaults were introduced.

## Typed result

The bounded original callback view covers +0x0000..+0x19DF (6624 bytes), excluding
padding before the next core entry at +0x1A00. The decompiler now displays:

- shared control, callback count, pending key/dirty mailboxes and voice registers;
- `me_mixer.voices`, noise fields, IRQ cursor/latch and block predictor tables;
- `MixerVoice` pointers with nested envelope, pitch and volume fields.

The callback remains raw decompiler output, not a compiled implementation or a
whole-function equivalence result. It retains warnings and untyped accesses into
unknown regions. Type names are descriptive recovery, not original identifiers.

## Protocol-ordering issue discovered during the pass

The first successful typed export used an ordinary byte-mapped shared alias.
Its C output moved reads from cached key-on/key-off/dirty fields below writes
that clear the uncached aliases, and removed the initial consumer-active store.
That output was readable but unsuitable as a sequential protocol description.
It was not installed into the native runtime.

The original instructions establish the required ordering: +0x54 writes the
consumer-active halfword, +0x5C..+0x64 polls the combined control word, +0x74 and
+0x84..+0x90 capture mailbox values, and +0xA8..+0xBC clear pending state and
release the consumer flag. The poll clears only bits 16..23 before comparing
with 0x100; a simple standalone producer-halfword comparison loses that detail.

Both shared analysis blocks are now marked volatile. This is a conservative
analysis annotation for asynchronous shared state, not proof that Sony's source
used the C keyword. The private mixer block is not made volatile. The updated
export preserves the initial `consumer_state = 1`, the combined polling mask
0xFF00FFFF, all four mailbox loads before their clearing writes, and the final
byte-sized release of `consumer_state`.

This focused check does not prove that Ghidra models all cache, alias or
cross-processor timing behavior. Preserve the original instruction evidence
when reconstructing another protocol path. A byte-mapped alias alone is not a
guarantee that the decompiler's optimization respects asynchronous observations.

## Verified outputs

Latest successful project:
`/tmp/repops-me-types.8yIohe/repops_me_types.gpr`.

Export directory: `out/ghidra-me-types.EuhMEh/`.

- `state/me_state.json`: completed decompilation, block initialization/alias/
  volatility flags and typed-state presence.
- `state/pops_me_audio_callback.c`: typed original listing, with warnings.
- `types/repops_types.gdt`: 13 root layouts (12 schema types plus the core view).
- `types/types.json`: field widths and offsets validated by the importer.
- `events/contracts.json`: the four event/timer GP contracts still pass after
  extracting the decompiler view into the common utility.
- `headless.log` and `project_path.txt`: actual run evidence.

The earlier nonvolatile export is retained at
`out/ghidra-me-types.xSq4Hl/state/`; its field names are informative, but its
mailbox order must not be used as a sequential reconstruction. The latest run
checked both shared prefixes (704 bytes), private prefix (6044 bytes), 13 root
layouts, event argument storage and the specific mailbox ordering above.

## Reproduce in a new project

```sh
ROOT=$(pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
PROJECT=$(mktemp -d /tmp/repops-me-types.XXXXXX)
RESULTS=$(mktemp -d "$ROOT/out/ghidra-me-types.XXXXXX")
printf '%s\n' "$PROJECT" > "$RESULTS/project_path.txt"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" repops_me_types \
  -import "$ROOT/build/pops_660.prx.dec" -noanalysis \
  -scriptPath "$ROOT/ghidra" \
  -postScript ImportStateLayouts.java "$ROOT/data/state_layout.json" "$RESULTS/types" \
  -postScript ApplyEventTypes.java "$RESULTS/events" implicit-core \
  -postScript ApplyMeStateTypes.java "$RESULTS/state" \
  > "$RESULTS/headless.log" 2>&1
test -s "$RESULTS/state/me_state.json"
```

Inspect `completed` and the generated output rather than trusting the headless
exit code alone. The script rejects an already-present synthetic state block;
existing analysis projects are not mutated to fit this new mapping. As with the
core GP view, the compiler adjustment is local to the decompiler, not a change
to the installed extension. Standard UI decompilation may require this scripted
view to reproduce the same field propagation.

Native execution remains stopped at the final ME postmix boundary +0x288 and
function-completion percentages are unchanged by this type pass.
