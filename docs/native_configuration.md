# Native per-game configuration and disc index

This pass completes and tests the pending working-tree changes that follow the
extended single-disc header. It does not run PS1 instructions or the ME mixer.
The original POPS input remains SHA-256
`6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0`.

## Reconstructed paths

- `src/native/pops_config.c`, original +0x24770: checks the required version,
  initializes GP-relative configuration, finds the normalized title key in
  the existing module table, applies settings and the supported header
  override. The input domain is null ID or uppercase AAAA12345 identifiers;
  unsupported or malformed table references stop explicitly. It is partial,
  not a replacement for all original string-conversion behavior.
- +0x1B6EC: configuration postprocessing with modulo-32-bit arithmetic and
  the two observed command-word sets. The EDRAM mapping in the harness is a
  shadow buffer for state writes, not a GPU or renderer.
- +0xADE8: the original nibble arithmetic for converting three MSF bytes to a
  sector number, including the -150 offset. Do not clamp malformed BCD bytes
  silently inside the function.
- The supported remaining +0x1B004 path updates disc selection and transforms
  block-index offsets. Auxiliary tables, extra entries and overflow paths
  remain explicit blockers. The original function is still marked partial.

Sources are the pinned `build/pops_660_reference.s`, original bytes and the
existing Ghidra outputs for +0x24770, +0x1B6EC, +0xADE8 and +0x1B004. In
particular +0x24924..+0x24930 increments GP+0x6C4 before updating GP+0x6D8;
this instruction order differs from the raw decompiler's apparent ordering.

## Validation

`make test-native-config` checks synthetic version refusal, null-ID setup,
matching configuration records, overrides, unsupported indices, postprocess
boundaries, both EDRAM word sets and block-index offset/read-size conversion.
It runs with AddressSanitizer and UBSan. These are contract tests, not a claim
of full binary equivalence. `make test-native-disc` continues to pass.

A fresh `./run_ffvi.sh` produced `out/ffvi_run.ZBc6fz/result/`. It matched the
actual title record at module +0xF1318, prepared 7,141 block-index entries and
stopped at +0x3764C (savedata metadata), after 18 instrumented native-function
entries. `game_executed` remains false. Previous result directories are intact.

## Next boundaries

The next sequential disc/startup work is +0x3764C, +0x24CC8 and +0x1C590, then
CD worker creation. The separate ME workstream remains mandatory for audio:
configuration progress does not validate the sample callback or worker.
