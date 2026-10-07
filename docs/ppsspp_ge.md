# Optional PPSSPP software GE backend

## Source and build

The donor is PPSSPP at `3c6c7f548b1582956478779952e424e2e3ed8ddd`,
checked out under `.tools/ppsspp-ge-source`. It is not included as firmware
and no PSP program is loaded into it. `scripts/build_ppsspp_ge.sh` verifies
that revision, initializes the required pinned submodules, applies the small
tracked patch and builds the bridge. FFmpeg, the SDL frontend and Discord
are not requested. CMake builds upstream libraries, including transitive
Core dependencies; this is not yet a minimal extracted GPU library.
The miniupnp headers are required by those transitive Core sources even with
UPnP disabled. On macOS the bridge also links the donor's Cocoa text unit,
which upstream normally attaches to frontends rather than to Common.

PPSSPP code is GPL-2.0-or-later. Its copyright notices are preserved in the
donor; `src/ppsspp_ge/LICENSE.PPSSPP` retains its license. `platform.cpp` is
adapted from PPSSPP's headless platform functions and carries that attribution.
The optional linked binary includes GPL code. This backend is separate from
the census of reconstructed POPS/POPSMAN functions and must not inflate it.

The tracked donor patch changes only build integration and the software
backend's EDRAM mapping/overlap masks. POPSMAN requests 4 MiB, whereas the
donor starts with a 2 MiB mapping and mirrors. The patch provides distinct
upper 2 MiB and retains the uncached aliases. No rasterization algorithm is
replaced, and there is no GP0-to-host drawing shortcut.

## Boundaries

`bridge.cpp` constructs PPSSPP's `SoftGPU` without a graphics window and
uses its actual enqueue, stall update, list-status and flush operations.
A MIPS state initializes the donor's GE timing support; neither a CPU run
loop nor an interpreter/JIT step is invoked. PSP firmware pages remain data
in RePops and its generated-code execution remains in Unicorn.

The optional `repops-native-ge` executable compiles the same native C sources
listed by the ordinary Makefile. Its RAM and 4 MiB EDRAM backing are borrowed
from PPSSPP's memory map. A 16 KiB scratchpad exchange surrounds GE operations
because RePops keeps that small block inline. Lists therefore see the live
pixel/vertex sources before the producer reuses them. Startup state lists
are copied to a reserved donor RAM staging address and executed rather than
merely captured.

Native queue ids use donor id+1 to preserve the existing zero-id sentinel.
The recovered POPSMAN provider keeps its polling and fallback logic. Its
host adapter currently uses the fallback rather than manufacturing a fast
completion bit: it releases the old list, queues the stalled continuation,
and checks actual completion before allowing pixel-dependent readback.
Uncompleted lists produce an explicit error, not fabricated pixels.

## Verification status

The bridge and optional native executable build successfully on the macOS
ARM64 host. `out/ppsspp-ge-smoke-01.log` records the first passed standalone
smoke: distinct upper EDRAM and uncached alias, a queue that remains stalled
before drawing, a software-rasterized red rectangle, 512 bytes transferred
back to RAM, and more than 64 completed lists with recycled queue ids.
These are actual SoftGPU results, not source-packet rendering or fixtures
that fill the expected framebuffer. No PSP CPU run/step function is called.

The focused capture-only GPU/display/readback checks and the three Python
runner checks also pass (`out/ge-hook-capture-checks.log`). The live FFVI
path is being exercised separately; a passed synthetic GE smoke is not an
FFVI boot, full PSP timing validation or a graphics-fidelity comparison.

Build with `sh scripts/build_ppsspp_ge.sh`; select `--ge-backend ppsspp` in
`scripts/run_native.py`. The default `capture` backend is unchanged. CMake
tracks the native Makefile source list so subsequent reverse increments are
included in both binaries rather than silently using an older source set.
