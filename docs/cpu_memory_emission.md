# Known-address memory emission

The saved category-0x10 work is now connected to the probe. The launcher had
attempted to append to `command` before creating it; `--emit-memory` now selects
both preparation and the memory-capable native emitter correctly.

`src/native/pops_emit_memory.c` reconstructs the constant-base path of +0x3A90,
the address-class helper +0x362C and its spill/argument/result helpers. Unknown
base registers still stop explicitly. These functions emit Allegrex words;
they do not themselves execute a PS1 write or provide a host CPU backend.

The initial BIOS stores at guest PCs 0xBFC0000C and 0xBFC0001C target
0x1F801010 and 0x1F801060. With the reset I/O-table fixture they become stores
to the GP-relative register shadow, at GP+0x2010 and GP+0x2060. The resulting
six Allegrex words occupy 24 bytes, including the preceding immediate loads.

`out/cpu_memory_resume_01/comparison.json` records a passing comparison with
the original routines: all 24 emitted bytes, the 49,168-byte record window
and all 16 KiB of scratchpad agree. The next nonempty category is 0x0E.

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_memory_new --emit-memory --compare
make test-native-emit
```

Scope: one initial BIOS prefix, with a reset-derived I/O table as probe input.
The earlier CACHE omission in compiler setup remains; no generated code,
hardware handler, dynamic-address access or full block controller is executed.
The integrated FFVI path has not been advanced by this isolated probe.
