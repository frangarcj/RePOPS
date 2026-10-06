# Flat polygons in the original GE consumer

The flat, untextured triangle/quad branch in POPS +0x133D0 now reaches its
original GE emission and work accounting. Source ranges are +0x140A8..+0x14230
and shared +0x13A98 onward, checked against the pinned disassembly and existing
decompiler output. Textured/Gouraud variants and oversized primitive splitting
remain explicit boundaries rather than silently using this branch.

`pops_gpu.h` names the command-plus-coordinate input, six-byte XYZ positions,
and 36-byte flat GE body. Input XY uses signed 11-bit components. The output
retains the optional draw-mode setup, offset command, color command and relative
jump to the original cached template. Inline X/Y data is written with the
existing little-endian accessors; vertex Z halfwords and the unused fourth
vertex of a triangle remain untouched.

Work calculation preserves the two triangle determinants for a quad, the
original bounds/compatibility gates and mask/semitransparency weighting. The
texture-cache traversal is shared with the existing fill branch; the polygon
path also marks an overlapping selected texture page stale. Both paths keep
the original grouped cache traversal and bank wrap.

Focused GPU checks cover GP0 packet buffering, exact GE words and relative
template target, a nondegenerate quad (230 work units), signed coordinates,
preserved Z and cache effects. Existing fill, DMA and display tests still pass.
These are native contract tests, not a rendered-frame or instruction-level
equivalence claim.

`out/gpu-flat.ubRtWo/result/` processes one real quad with 153,630 work units,
then reaches the deferred DMA callback +0x8CAC. The final diagnostic records
40,070,894 generated-code observations, 43,291 transfers and
`game_executed: false`. GE data has been produced, not rendered. This extends
the existing partial +0x133D0 body and does not add a new original function.
