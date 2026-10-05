# What POPS does with its instruction records

## Reconstructed prefix

`src/native/pops_compile.c` reconstructs +0x58C0 through +0x5D5B, immediately
before allocation/emission begins. It preserves the original staged pipeline:

1. Choose RAM or BIOS source mapping and the corresponding code-cache cursor.
2. Set a bounded analysis window and call the reconstructed +0x05154.
3. Mark the covered guest-page range in GP+0x1D8.
4. Refine operation costs and accumulate them at control-flow boundaries.

RAM code-cache rollover and invalid-PC exception dispatch are explicit stops;
this prefix does not pretend those dependencies exist. The returned cursor is
where code could be emitted, not a pointer to an executable compiled block.

## Why the second pass matters

The first pass writes per-operation cost metadata at record +15. The second
looks ahead over subsequent instructions for selected load, multiply/divide
and GTE cases. It accounts for intervening work until a dependency or boundary,
then stores accumulated costs at record +6. This is the recovered accounting
algorithm, not a measurement of real console timing or host speed.

It also adds 0x8000 and 0x2000 flags under the original conditions. Their exact
later effects must be followed through the emitter; they are not invented
host-optimization hints. The BIOS sample records cost 90 at PC 0xBFC00074 and
180 at the terminal marker 0xBFC00240.

## Remaining phases observed in +0x058C0

After +0x5D5C, POPS initializes allocation tables at GP+0x740 and following,
and resets known-value tracking at GP+0xB58/0xB5C. Its record walk calls an
emitter at +0x6914 and register helpers including +0x2E9C. It recognizes some
constant branch conditions and handles load/delay-slot dependencies. Finally
it patches relative/absolute branches, publishes entries in code-lookup tables
and updates the allocation cursor. These phases are observed, not reconstructed
by the current prefix.

Record +4/+6 is reused later as a generated-code address in some paths. Thus
the scratch records are a mutable compiler work area, not a permanent portable
IR with one unchanging schema. A backend decision must not obscure that fact.

A small helper at +0x44DC emits `ADDIU T9,T9,-cost` for positive costs. This
connects the accounting pass to generated code: POPS charges a batch of guest
work through the host register T9. Its full timing/dispatch contract is still
being traced. This helper is not yet reconstructed in the current prefix.

## Reproduce the focused comparison

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_prepare_new --prepare --compare
```

`out/cpu_prepare_checked_01/` matches all 49,168 compared record-buffer bytes
and the 16-KiB scratchpad for the initial BIOS sample. The original routine is
stopped at +0x5D5C before CACHE or emitters run; its S1 points at 0x09B80000.
The native implementation reaches the same boundary logically. This is one
sample with generic MIPS-compatible instructions, not exhaustive instruction
coverage, complete compilation, or PS1 execution. The initial native +0x05154
probe remains available without `--prepare`.
