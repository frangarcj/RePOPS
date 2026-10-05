# POPS instruction analysis, +0x05154

## Scope and result

The goal is reverse engineering and an implementation of POPS. Backends or
provisional executors are means to keep that work moving, not a separate
project. This pass preserves the existing analysis representation instead of
designing an LLVM/ARM representation before understanding it.

`src/native/pops_analyze.c` reconstructs +0x05154..+0x58BF, including the opcode
classification switch, recursive target discovery, stopping rules and backwards
annotations. Source-dependent register numbers are still numeric. The C routine
analyzes instructions; it is not a MIPS interpreter or replacement game code.

`src/native/analyze_main.c` supplies BIOS-specific inputs established by the
beginning of +0x058C0. That surrounding compiler routine is not reconstructed:
the probe is a driver, not an implementation of +0x058C0.

The checked run `out/cpu_analysis_checked_01/` produces 145 address-indexed
record slots, of which 91 have nonzero categories. They include a terminal
boundary record, not 145 or 91 executed instructions. The analyzer enters
twice, following the jump at BIOS PC 0xBFC00070 to 0xBFC00150. Its high-water
record address is 0x041B0900, corresponding to PC 0xBFC00240. The latter is a
category-5 boundary after the Status-register write at BIOS offset 0x23C.

## Recovered layout

Record address is `0x041B0000 + 4 * (guest_pc - analysis_base_pc)`.
Each guest 4-byte instruction occupies one 16-byte analysis slot.

| Offset | Width | Observation in +0x05154 |
| --- | --- | --- |
| +0 | 16 bits | Flags, including delay-slot, target and boundary annotations. |
| +2 | 8 bits | Destination/link register; some special cases use 0xFF. |
| +3 | 8 bits | Normalized operation ID, not always the original major opcode. |
| +4 | 16 bits | Consumer category; numeric names retained. |
| +6 | 16 bits | Not written by this producer; the next compiler pass stores accumulated boundary cost here. |
| +8 | 32 bits | Original instruction word, replaced by target PC for direct branches. |
| +12/+13 | 8+8 bits | Source-register fields, cleared or adjusted by category. |
| +14 | 8 bits | Additional coprocessor/register field where required. |
| +15 | 8 bits | Per-operation cost field plus GP+0xB42; not independently validated as timing. |

SPECIAL operation IDs use `funct | 0x40`. Conditional branch IDs are mapped
to 0xC0..0xC5. Some trapping arithmetic operation IDs are normalized to their
nontrapping counterpart while the payload retains the original word. Do not
infer complete execution semantics from that normalization alone.

The jump at 0xBFC00070 has flags 0x0004, category 0xE and payload 0xBFC00150.
Its delay slot has flags 0x0011. The target has flags 0x0208. The backwards
scan adds 0x80 to certain earlier records. These retain the original algorithm,
not a newly designed control-flow representation.

## Focused original/C comparison

`scripts/probe_cpu_analysis.py --compare` checks source/image fingerprints,
runs the native C probe, and executes the original analyzer from the relocated
image in Unicorn 2.1.4/MIPS32 24KF. BIOS words are input data to both runs.
The original path reaches its normal return without external service stubs.
Comparison covers 49,168 record-buffer bytes (including unused zero slots)
and all 16 KiB of scratchpad: no differences for this input.

This is one BIOS sample, not all opcode cases, whole-emulator equivalence,
cycle validation, or a claim that generic MIPS supports all Allegrex features.
The isolated oracle overlays scratchpad onto unused base-zero module bytes;
the native runtime keeps those address spaces separate. The wrapper keeps
firmware, records and reports under ignored paths and refuses existing outputs.

## Next boundary

Recover +0x058C0's passes over these records and its Allegrex emission. The
current records describe PS1 operations with compiler annotations; we have not
established that every downstream transformation is host-neutral. Generated
Allegrex execution may use a temporary, explicitly identified bridge rather
than blocking POPS reconstruction on a polished backend. The integrated FFVI
trace still stops at display refresh +0x115B4; this separate analyzer probe
does not pretend that blocker is resolved.

Follow-up: `pops_compile.c` now reconstructs the surrounding setup and the
cost pass through +0x5D5B. Use `--prepare` with the probe; see
`cpu_compiler_stages.md`. Emission and linking are still pending.
