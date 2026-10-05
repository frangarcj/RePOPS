# POPSMAN / POPS 6.60 interface

## Loader boundary

The 6.60 `scePopsManLoadModule` implementation has two parameters:

- `a0`: EBOOT/PBP path passed into the manager path setup.
- `a1`: mode; mode 2 skips loading POPS, otherwise the hardware-model-specific module is loaded.

For the normal path POPSMAN:

1. calls `sceKernelGetModel()`;
2. formats `flash0:/kd/pops_%02dg.prx` with model + 1;
3. calls `sceKernelLoadModuleForKernel(path, 0, NULL)`;
4. calls `sceKernelStartModule(modid, 0, NULL, NULL, NULL)`.

The key point for rehosting is step 4: POPS receives **zero module-start arguments**.

## Direct POPSMAN dependency from POPS

PSPLibDoc 6.60 shows POPS importing only one function from library `scePopsMan`:

- NID `0x0090B2C8` - `scePopsManExitVSHKernel`

This is a count for a **library name**, not for the provider module. POPSMAN
also exports `sceMeAudio`. Joining all measured POPS imports against the
6.60 POPSMAN exports produces **30 matches: 29 sceMeAudio + 1 scePopsMan**.
The manager therefore remains a significant runtime dependency. A zero-length
start argument does not establish that the emulator has a self-contained API.

Reproduce this metadata cross-check with:

```sh
python3 scripts/audit_contract.py out/analysis.json build/popsman_exports_660.xml
```

The export metadata is from `Spenon-dev/PSPLibDoc`, commit
`c2834ef4abf881b95fb2eb798b85bb94cf195eb6`,
`PSPLibDoc/6.60/Export/kd/popsman.xml`. Attribution is by library/NID, not a
runtime observation of the loader.

This has now also been checked against the actual export table of the pinned
ARK POPSMAN reference. `scripts/map_popsman.py --out <new-directory>` reports
the 30 exact matches and static caller sets. The provider includes modified
export library names (`noAudio`/`noAudio_driver`), so it must not be described
as authenticated stock firmware. See `popsman_contract.md` for provenance,
graphics/file services hidden behind `sceMeAudio`, and three bounded leaf
implementations tested against original instructions.

## POPS module metadata

From the decrypted 6.60 ELF:

- entry: `0x00016000`
- export table starts at `0x0003D4A4`
- import descriptor table: `0x0003D4BC..0x0003D690`
- initial analysis window: `0..0x0003D4A4`; this is not proof of the complete code extent
- 159 imported functions in 23 libraries, including the final `UtilsForUser` descriptor

The historical ARK disassembly contains 656 subroutine markers. 489 are still generic `sub_XXXXXXXX` names, which is a useful baseline for measuring our own function recovery.
