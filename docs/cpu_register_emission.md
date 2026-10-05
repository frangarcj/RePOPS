# Original register allocation and first Allegrex emission

## What was reconstructed

`src/native/pops_emit.c` contains C reconstructions of the register-location,
temporary reservation, reload/spill, constant materialization, pair allocation,
HI/LO flush, cycle debit and delay-slot helpers used by POPS. The implementation
preserves the original numeric scratchpad fields and produces Allegrex words.
It does not translate to ARM64 or execute the resulting instructions.

The category-9 immediate path of +0x6914 now calls those C helpers, as does the
no-output category 0x13. Other categories stop explicitly. The full +0x058C0
record walk is still pending: the probe calls the available emitter directly.
Initialization of the tables is a selected setup sequence inside +0x058C0,
not an extra original function to count in the progress register.

## The mapping is not one-to-one MIPS execution

POPS copies a signed mapping table from module offset 0xD40C8 to GP+0x778.
Nonnegative entries identify fixed Allegrex integer registers. Entries below
-1 identify FPU registers used to hold integer bit patterns; for example a
mapping of -20 produces MFC1/MTC1 transfers involving F20. -1 identifies the
memory-backed path. The reload instruction is not floating-point arithmetic.

Twelve temporary slots at GP+0x760 use the host-register table copied from
0xD40E8 to GP+0x76C. Dirty flags are at GP+0x754. Allocation reuses an existing
slot or chooses a candidate using protected-register bits, location, dirtiness
and constant knowledge. The exact original sentinel/selection behavior is
preserved, including slot zero's special role. This is not a new allocator.

Known guest-register values are tracked in GP+0xB58's high-to-low bitmask and
GP+0xB5C's value array. The constant emitter can use a small immediate, GP/SP
conventions, an existing known register, or a LUI/ORI pair. It also recognizes
adjacent instructions and some load/store forwarding opportunities. Broader
claims about optimality or overall performance require actual execution data.

## First measured output

For the initial BIOS sequence:

```text
PS1:       LUI t0,0x13 ; ORI t0,t0,0x243F ; LUI at,0x1F80
POPS out:  LUI s0,0x13 ; ORI s0,s0,0x243F ; LUI k1,0x1F80
words:     3C100013      3610243F            3C1B1F80
```

The first LUI/ORI pair is folded as one known value in the compiler: guest t0
is known to be 0x0013243F and the next record's category is cleared. Loading
that value still takes two Allegrex instructions in this case. The next LUI
sets guest at's known value to 0x1F800000. The emitted words total 12 bytes.
The following PS1 store is category 0x10, which is not reconstructed yet.

`out/cpu_emit_immediates_04/` contains the passing comparison. The native C and
the original routines agree on all 12 emitted bytes, the 49,168-byte record
window and all 16 KiB of scratchpad for this sequence. This does not establish
all helper paths or all instruction categories as equivalent.

The oracle runs the original analysis/cost pass, skips the one CACHE at
+0x5D5C, then runs register-table initialization through +0x5E77. An observation
hook stops at +0x5E78 without changing guest state, avoiding a stale reported
PC in consecutive Unicorn `until` runs. Original +0x6914 and its helpers then
run normally on the selected records. No emitted Allegrex instruction is
executed, and no PSP GPU, OS or device behavior is inferred from this test.
Earlier incomplete comparison attempts are preserved, not overwritten.

## Reproduce

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_emit_immediates_new --emit-immediates --compare
make test-native-emit
```

The small synthetic smoke test covers fixed/FPR/memory locations, FPR spill,
slot state, immediate reuse and a cycle debit. It passes with ASan/UBSan;
it is separate from the original-byte comparison and is not exhaustive.
The preparation-only comparison still passes after this addition.

The integrated FFVI diagnostic remains stopped at display refresh +0x115B4.
The CPU emitter probe is independent of that incomplete path. Next useful
reverse target is the category-0x10 load/store emitter and its +0x3A90 helper,
then control-flow emission and the block controller/linker.
