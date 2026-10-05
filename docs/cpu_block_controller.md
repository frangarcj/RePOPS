# BIOS block controller

`rp_pops_emit_block_records` reconstructs the BIOS path of +0x58C0 from
+0x5E78 through +0x64F7. Unlike the individual-record probe, it follows the
controller's joins, look-ahead allocation, delay-slot order and boundary costs.
It saves each category before overwriting record+4 with an output address.

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_walk_new --walk-block --compare
```

The first run, `out/cpu_walk_resume_01/`, matches 352 emitted Allegrex bytes,
the 49,168-byte record buffer and all 16 KiB of scratchpad. The original
controller is executed normally between the selected boundaries, including
its calls to +0x6914. Cache maintenance at +0x5D5C and +0x5E80 is omitted.
No emitted code is executed. This is one BIOS configuration, not validation
of all joins, dynamic addressing, RAM blocks or delay-slot hazards.

## Final linking and publication

`--compile-block` now includes +0x64F8 through the return: patch forward links,
publish BIOS cache entries, clear the record workspace, debit preparation cost
and advance the cache cursor. `rp_pops_publish_bios_block` consumes the reset
VFPU row from the native context instead of assuming any arbitrary vector is
zero. The probe supplies that documented reset precondition explicitly.

`out/cpu_compile_resume_01/` matches the 352-byte output, 49,168 record bytes,
16 KiB of scratchpad, 2.5 MiB of code-cache tables and returned entry 0x09B80000.
The cache pointer advances to 0x09B80160. Original SV.Q R403 instructions at
+0x665C/+0x666C are handled by a narrow 16-byte-store adapter outside emulation;
the original reset at +0x1C580 establishes R403 as zero. CACHE operations are
omitted and listed in the report. No emitted Allegrex code is executed.

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_compile_new --compile-block --compare
```

This completes one supported BIOS compiler path, not every path through the
compiler. RAM profiles, more instruction categories, dynamic bases and complex
delay slots remain explicit boundaries. This result must not be reported as
PS1 BIOS execution or as an already portable executable code block.
