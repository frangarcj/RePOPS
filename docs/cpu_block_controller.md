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

## Integrated reset

The same C compiler is now linked into `repops-native`, not just the analyzer
probe. The end of +0x24B58 resets 2 MiB of guest RAM, then +0x94C4 selects
the reset exception vector and compiles it on a cache miss. Its returned
entry is stored at GP+0x1B4. This run uses the actual reconstructed device
initialization and game configuration, not the probe's I/O-table fixture.

`REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh` produced
`out/ffvi_run.VzSqXd/result/`: initial disabled-display command list, GPU
handler registration, block publication at 0x09B80000 (352 bytes), then
`startup_thread_handoff_pending` at +0x1A908. The original routine waits on
module state +0x14CC64 written by another thread. The diagnostic UI bypass
does not satisfy that wait, and this run does not force it to.

Normal startup still stops at unreconstructed +0x28DF8. If the real producer
later signals completion, a separate explicit boundary remains at +0x1A00:
executing generated Allegrex on the native host is not implemented. The
integrated run is a host execution test, not an original-vs-C comparison of
the entire reset or the headless display services.

The same diagnostic path passed an AddressSanitizer/UBSan build in
`out/ffvi_run.6wFNWG/result/`. A subsequent optimized build preserved the
normal UI stop in `out/ffvi_run.uK0qcD/result/`. The Python suite ran 49 tests
with one optional-Unicorn skip, and the existing emitter smoke passed.
