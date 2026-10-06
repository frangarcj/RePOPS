# Offline diagnostic view of emitted GE geometry

REPOPS_GE_PREVIEW_TRACE=1 enables read-only observations of the flat-polygon
GE records after native POPS has emitted them. The optional script
scripts/preview_ge_geometry.py reads those records, not original GP0 packets,
and produces a color view, wireframe and provenance.json in a new directory.
It requires Pillow only in the analysis Python environment.

Example using an existing recorded execution:

```sh
.tools/verify-env/bin/python scripts/preview_ge_geometry.py \
  out/gpu-upload.Kw2Nqo/result/trace.jsonl --out out/ge-preview-new
```

The first exported view, out/gpu-upload.Kw2Nqo/preview/geometry.png, contains
280 emitted polygons at frame_counter 117. Its recognizable PS1 BIOS logo
comes from the captured vertices and colors. The trace digest, chosen counter
and omitted effects are recorded alongside the images.

This is not the emulator's framebuffer, a screenshot of a booted game, or
execution of the PSP GE. The viewer uses an offline raster approximation
over a 1024x512 coordinate canvas with captured drawing offsets and scissor.
Textures, blending, dithering, exact edge rules, other primitives and display
scanout are absent. No pixels from this viewer feed back into emulation.
