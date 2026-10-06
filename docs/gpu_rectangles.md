# Rectangle GE records and texture preparation

The mode-3 branch of +0x133D0 now reconstructs the rectangle path at
+0x14760..+0x14FC8. It handles variable dimensions and the encoded 1/8/16
sizes, signed-coordinate wrapping after drawing offsets, clipping-dependent
work, the one-row compatibility gate and display-intersection state.

Textured rectangles retain the original palette load, texture-window words,
cache conversion calls and inline sprite layout. Cold indexed pages mark the
original 3+3+3+3+3+1 cache groups and emit conversion commands; warm pages do
not repeat those commands. The unaligned window branch uses its original
intermediate buffer. Direct 16-bit pages keep their VRAM address and format.
The renderer boundary remains GE list memory, not a new host texture API.

The 60-byte textured GE view preserves the overlapping writes involving the
first Z word and second UV pair. It also preserves source U alignment, raw
texture color, mask/blend-dependent template destinations and the final
texture-scale restoration. Plain rectangles retain their 24-byte record and
do not overwrite the unused Z halfwords.

Recovered names now include texture_depth (core +0x3656), the signed texture
offset-word biases (+0x714), and the texture color-word mask (+0x6D4). They are
used in the source and in the previously reconstructed reset/refresh paths.
These names describe recovered behavior, not original Sony identifiers.

Focused native fixtures compare exact cold/warm GE words, the CLUT address,
inline vertex words, work values, cache tags, unaligned windows, 4/8/16-bit
paths, raw-color selection and plain-rectangle Z preservation. Build, GPU
and display checks pass with the existing sanitizers. This is not a proof
of all rectangle paths or GE rasterization; +0x133D0 remains partial.

The integrated result and next reached boundary are recorded in progress.md.
