# Reuse Unicorn instead of extending the provisional interpreter

The current native executable links **Unicorn 2.1.4** through its C API.
`generated_unicorn.c` replaces `generated_code.c` in `NATIVE_SRC`; the latter
is retained only as the earlier experiment and is not linked into production.
No Unicorn source or binary is added to Git.

The reconstructed POPS analyzer/compiler and native helpers are unchanged.
Unicorn runs the generated Allegrex-subset cache using its MIPS32 24KF model.
It shares host-owned RAM, scratchpad and EDRAM buffers through `uc_mem_map_ptr`.
Only generated cache pages are executable; original module pages are data-only.
Known helper PCs are explicit Unicorn exits. After `uc_emu_start` returns,
registers are synchronized and the existing C helper dispatcher takes over.
Callbacks never change guest PC/registers while the engine is executing.

An early attempt to resume after a fetch-permission fault crashed inside
Unicorn's TCG code generator. Using explicit exit PCs avoids that path for
known returning helpers. The permission boundary remains a safety net for
unknown original targets, which our dispatcher stops without resuming.
When adding a returning helper, also register its PC in the exit array.

Native compiler writes and link patches invalidate Unicorn's translated
cache before re-entry. Address arguments to its variadic cache-control API
are explicitly 64-bit. This is a straightforward diagnostic bridge, not a
performance-oriented integration or an independent emulator project.

The first successful integrated run is `out/ffvi_run.mbYN9U/result/`:
BIOS PC BFC0039C, five compiled-entry transfers, 731 instruction-hook events,
and the same +0x3A90 dynamic-base emitter boundary as the previous executor.
Hook events are instrumentation observations, not guaranteed retirement counts.
`game_executed` stays false. The PSP UI bypass remains explicit.

## Build and focused check

Install the pinned dependency from `requirements.txt`. Make discovers the
Python package in `.tools/verify-env/bin/python`, falling back to `python3`;
`UNICORN_PYTHON` and `UNICORN_ROOT` can select another local installation.
The tested macOS wheel contains C headers and `lib/libunicorn.a`. Linking that
archive removes the need for a Python process or Unicorn shared library at run
time. The dependency's own license applies; this project does not republish it.

```sh
make native test-unicorn-cache
REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh
```

The focused test checks two helper returns, a delay-slot store, shared memory,
FPR bit preservation and a native patch of previously translated code.

## Limits

MIPS32 24KF is **not Allegrex**. The observed BIOS path uses a supported common
subset; this is not a claim of complete PSP VFPU, cache, exception or unusual
opcode compatibility. An invalid instruction stops the diagnostic. Some ISA
differences can share valid encodings, so broader execution still needs review.
Unicorn also does not implement our PS1 GPU/SPU, POPS helpers or the original
compiler for us. Those remain reconstructed C work.

Upstream overview: https://www.unicorn-engine.org/
API definitions used: the pinned package's `include/unicorn/unicorn.h` and
`include/unicorn/mips.h`. No new proprietary firmware dependency is introduced.
