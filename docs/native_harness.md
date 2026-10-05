# Native C execution on macOS

## Goal and current path

The goal is to reconstruct PSP POPS-related PRX functions in C and execute
that C on Mac. Original Allegrex code is used for analysis and bounded
comparisons, not interpreted by the native executable. Earlier hybrid-PRX
and Media Engine experiments remain in Git as research, not the active target.

The local `make native` build produces a Mach-O arm64 executable on this Mac.
`run_ffvi.sh` at the repository root builds and runs it against the user's
local PS1-for-PSP PBP. It preserves every trace under a new output directory.

Observed with the supplied Final Fantasy VI PBP:

1. The reconstructed `module_start` at POPS +0x16000 queues the C thread.
2. The reviewed +0x16080 prefix prepares headless callback handles.
3. +0x24A08 copies and patches 159 synthetic import stubs in the data image.
4. +0x1B2F0 / +0x1B56C initialize the cache list and open the local PBP.
5. +0x36CF4 reads 7,032 bytes of ICON0 at file offset 984.
6. +0x39314 / +0x390A8 and a native libpng adapter recover an 80-by-80 icon.
7. +0x28790 stores the observed low-byte-derived tag.
8. The single-disc PSISOIMG header is read at file offset 65,536.
9. +0x1B004, +0x287C4 and +0xDEFC select disc zero and load its header into
   guest address 0x09E80000.
10. The explicit unprotected-format adapter for `sceMeAudio_14447BA0` reads
    another 735,360 bytes and reconstructs the observed single-disc field
    updates. It does not implement the protected-file/BBMac path.
11. +0x28730 preserves the provider result. +0x1B61C and +0x1AF90 normalize
    and validate `SCES03828`.
12. Execution stops at the per-game configuration routine +0x24770. The active
    disc byte is not updated prematurely to pretend that configuration passed.

The successful diagnostic path is recorded in
`out/ffvi_run.c93OG9/result/`. It reports 15 instrumented function entries and
14 logged host calls. These are not complete-function coverage, CPU coverage,
or percentages of the overall project. Several C functions are explicit
prefixes ending at an unreconstructed callee.

There is no PS1 instruction execution, gameplay, framebuffer or SPU output yet.

## Important memory correction

The PSP scratchpad is at 0x00010000..0x00013FFF. POPS uses GP=0x10000 for
scratchpad-relative state. The same numerical offsets also fall inside the
base-zero Ghidra module image. The first native implementation incorrectly
overlaid these two address spaces, making the disc-offset table read a code
word, 0x000C1940, instead of scratchpad state. That caused a spurious seek to
858,432 and an apparent firmware error in `out/ffvi_native_03/`.

`rp_context.scratchpad` is now distinct. Guest accesses in that range use it;
code introspection explicitly uses `rp_module_memory` / `rp_module_u32`.
The build-string buffer at GP+0x3FC0 is bounded by the actual 64 bytes left
in scratchpad. `tests/test_native_memory.c` checks both separation and bounds.
The harness initializes its synthetic scratchpad to zero; this is a host
startup policy, not a measurement of power-on PSP memory contents.

This is still a partial analysis address model, not a full PSP address space.
Future functions must distinguish module-relative addresses, absolute PSP
addresses, aliases and pointers before being connected.

Reference: JPCSP `src/jpcsp/MemoryMap.java`, commit
`00ac4d6daedd89c0492093ae83fb0d3d819af414`, declares the scratchpad bounds.
The original POPS instructions at +0x1B068/+0x1B06C show the GP-relative
disc-offset and PSAR loads. No disc offset was manually forced to pass the run.

## Explicit host adaptations

- Thread creation/start is a cooperative single-C-thread queue, not a PSP
  scheduler. Only the known startup entry is dispatched.
- Power and storage callbacks are recorded as handles in a headless
  environment. Their asynchronous behavior is not implemented.
- Import slots are metadata for the reconstructed self-patcher. Preparing 159
  slots does not mean 159 services have been implemented.
- The file path/PSAR offset and DATA.PSP first word are supplied by a bounded
  host PBP adapter. POPSMAN's full startup, UID object flags and protected-file
  semantics are not reconstructed. The extended-header adapter admits only a
  bare-ELF DATA.PSP and a structurally supported plain single-disc header;
  those checks are format admission, not authentication.
- Reading BC100040 for the provider's ME bootstrap mode is explicitly deferred,
  not replaced with a made-up hardware value. See `native_disc_header.md`.
- `scePaf`'s observed libpng metadata path uses the installed native libpng,
  not the old PSP libpng ABI. Pixel decode is absent. Native cleanup differs
  from the apparent original early-return allocation lifetime.
- Unsupported paths stop through a recorded blocker; they do not return a
  fabricated success result.

The header service is at provider +0x050C and its reader at +0x0D3C. A fresh
Ghidra import of the corpus provider, followed by a seeded read-only pass,
confirms the field-update path used for the reconstruction. The supported
native path explicitly separates plain file I/O from the original protected
reader and BBMac branch. The earlier ARK provider remains a distinct input.

## Firmware corpus

An archived copy of the public PSP 6.60 updater was downloaded from:

`https://archive.org/download/psp_ofw_firmwares/PSP/660.PBP`

The historical publisher endpoint failed; the final archived download used
verified TLS, validated range sizes, and the historical package MD5. This is
provenance/fingerprint checking, not a new digital-signature verification.

- Updater size: 32,608,261 bytes.
- MD5: `2ca64d59dcf48f45fb99b400a586b395`.
- SHA-256: `ac8ae9b03612218e224c16fd0297ad879dc17cb6cfe848f1d8fa289015c71e59`.
- Local input: `input/firmware_660/EBOOT.PBP`.
- Decrypted corpus: `build/firmware_660/F0/`.

There are 288 PRX files; all parse with the current inventory tool. Starting
from `pops_01g`, `popsman` and `libpspvmc`, a conservative static dependency
superset contains 114 modules. It retains 668 ambiguous function-provider
edges, 10 unresolved function imports and 10 variable-import groups. It is
**not a validated load order**: model alternatives and dynamic loads remain.
Full details are in `out/corpus_660/corpus.json`.

Core SHA-256 values:

| Module | SHA-256 |
| --- | --- |
| `kd/pops_01g.prx` | `6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0` |
| `kd/popsman.prx` | `ff4222e8085190e1f357aece096282963e0e8e317b76547fafb1600a746a237e` |
| `vsh/module/libpspvmc.prx` | `bf24ffbeecc72a9c485c98db71d0d26de435b703357eb346fc0a458fc0d34061` |

POPS itself matches the earlier ARK analysis binary exactly. POPSMAN does not:
the old provider hash starts `83ed5373` and has patched `noAudio` export names.
Do not silently attribute all earlier provider tests to the corpus version.

## Reproduce

```sh
./run_ffvi.sh
./run_ffvi.sh "$HOME/Downloads/Final Fantasy VI.PBP"

# Explicit data image and result locations:
sh scripts/prepare_native.sh out/native_image_new
python3 scripts/run_native.py --image out/native_image_new \
  --pbp input/games/final_fantasy_vi/EBOOT.PBP --out out/native_run_new

# Synthetic regression for the address-space collision:
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/native/runtime.c tests/test_native_memory.c -o build/test_native_memory
./build/test_native_memory
python3 -m unittest discover -s tests -v
```

The launcher checks input/image provenance, preserves prior output directories
and records `game_executed: false`. Exit 78 from the native binary means a
diagnostic stop. Exit zero from the Python/root wrapper only means the stop
and its report were captured successfully.

The user-supplied FFVI file is local and ignored. Neither it, extracted game
assets, firmware, generated Ghidra data nor the bulk decompiler output should
be staged. Only source, tests, scripts and research notes belong in Git.
