# NCDS normal color and depth cue

Command 0x13 selects +0xFA68, whose body ends at +0xFBA4. The emitter jumps
directly to it without flushing GPR temporaries or HI/LO: the helper uses VFPU
arithmetic and only changes scalar V0/A0/A1/A2. The native exit preserves that
contract rather than clearing the register allocator indiscriminately.

The recovered register prefix now names source RGBC, the RGB FIFO, light and
color matrices, background color and far color. The C accesses these names
through existing guest-memory accessors; no host-pointer overlay is used.
The ninth element of each matrix follows the original full-word VI2F.S load,
while the first eight follow the signed-halfword VS2I conversion.

## Stages and arithmetic

The body transforms the normal through the light matrix, clamps it, applies
the color matrix plus background, modulates by the input color, and blends
toward far color using IR0. It retains the distinct signed difference limit,
nonnegative IR limit and RGB limit, including the inclusive difference FLAG
comparison. MAC/IR integer conversion rounds nearest-even; packed RGB uses
the signed-integer conversion followed by VI2UC's upper-byte extraction.
RGB0/RGB1 shift and RGB2 receives the new result with the original code byte;
MAC0, IR0, the FIFO padding and the memory FLAG shadow remain unchanged.

The finite-input dot model follows the two-guard-bit product/accumulation
behavior described by PPSSPP's `vfpu_dot_reference`, not host fused multiply-
add. Scalar stages remain separate FP32 operations. This is a reviewed partial
reconstruction: the fixtures do not prove complete hardware VFPU equivalence,
prefix-state equivalence or all rounding-edge behavior of the whole helper.

Primary arithmetic references read for this increment:

- PSPDEV VFPU documentation, https://pspdev.github.io/vfpu-docs/ : VF2IN,
  VI2UC, VTFM and unaligned vector stores.
- PPSSPP commit `3c6c7f548b1582956478779952e424e2e3ed8ddd`,
  `Core/MIPS/MIPSVFPUUtils.cpp` (`vfpu_dot_reference`) and
  `Core/MIPS/InterpreterVFPU.cpp` (conversion/transform semantics).
  Ignored local copies are in `build/reference/`; their SHA-256 values are
  `58bb486d5ae2032b257ec1be36c2bd63ba2b6094f406625302ed842c18e937bf`
  and `5780176ac50e0dd20801359a403d6452a38edb7eeeb0efc19375e67643238ce6`.

## Validation and execution

Seven focused fixtures cover identity lighting, depth cue and channel flags,
non-symmetric matrices, negative input, half-integer ties, an inclusive limit
and the full-word ninth coefficient. They check MAC/IR, RGB packing/FIFO and
unmodified live scalar state. Emitter and Unicorn checks also pass.

`out/gte-ncds.SrXOEA/result/` executes 560 NCDS calls, 560 RTPT calls, 560
NCLIP calls and 279 AVSZ3 calls. It reaches active-display refresh +0x115B4,
with 39,939,594 generated-cache observations and 41,765 entry transfers.
The first recorded colors are black with the preserved 0x20 code byte; no
visible shading or framebuffer is inferred from these calls. The report
still records `game_executed: false`.
