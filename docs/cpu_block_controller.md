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

The next boundary is +0x64F8: resolve forward links, publish code-cache entries,
clear records with the reset VFPU row, advance the cache cursor and return the
compiled entry. That final pass is not included in the above result.
