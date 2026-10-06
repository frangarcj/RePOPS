# Active display lists, not a new renderer

The internal-screen 16-bit branch of POPS +0x115B4 is reconstructed in
`src/native/pops_display.c`. The caller in `pops_graphics.c` retains common
frame bookkeeping, texture-cache maintenance, draw-state restoration and
the existing headless display services. No GP0 command is drawn directly
by the host and no captured GE operation is presented as rendered pixels.

## Recovered branch and layouts

Source evidence is the pinned original disassembly at +0x11BF4..+0x122F8
and its +0x12310/+0x12320/+0x12448 continuations, cross-checked with the
existing decompiler output for +0x115B4. Tables remain in the input image.
The native header names the nine-byte resolution row, six-byte signed
display-fit row, and twenty-byte pair of ten-byte GE vertices.

The resolution width field is in units of sixteen pixels: dividing it by
four gives the number of sprite pairs, not the byte's value itself. Only
the eight fields actually written by the original vertex loop are updated;
the two Z halfwords remain untouched. Guest addresses stay numeric and
all wire writes use the existing little-endian accessors.

The branch keeps the texture-origin fold versus vertical wrap, display
mode/fit selection, signed coordinate scaling, compatibility-dependent
clipping, optional border-clear lists, background shading, and the split
primitive submission selected by a negative count descriptor. It captures
the POPSMAN E7F06E2B stall request at the original publication point before
updating vertex data. This is a platform adapter, not execution of that
provider body or of the GE list.

The touched common refresh path now also names its GPU, clock, shared-audio
and display-config fields. This replaces local raw GP offsets without
changing the firmware memory layout or performing a global refactor.

## Verification and scope

`make test-native-display` checks exact GE words and nonzero source/destination
coordinates with synthetic tables. It also checks border clear/background,
vertical wrap, wide-mode split submission and rejection of unreconstructed
24-bit display. The fixture preserves prefilled Z words and runs with ASan
and UBSan. These are contract tests, not original-instruction differential
tests or hardware rendering verification.

The initial integrated run `out/display-active.G1CfFs/result/` passes active
refresh and reaches a flat quadrilateral packet (0x28000000) in +0x133D0.
It records 39,944,042 generated-code observations, 41,834 transfers and
`game_executed: false`. The original +0x115B4 body remains partial; 24-bit,
external output, active UI and other lifecycle paths are separate work.

After migrating the common caller to named fields, the final repeat at
`out/display-typed.Aw1FII/result/` gives the same blocker and counters. Native
display, GPU, GTE and Unicorn checks pass with that source state.
