# BIOS byte reads and base-zero code links

The reached +0x1A90 path now reads signed bytes from RAM, the expansion/default
helper, and BIOS/scratchpad specialization. A BIOS read patches the generated
call site to +0x1AC8, as the original does, and subsequent calls exit Unicorn
there before executing reconstructed C. I/O specialization is not yet covered.

The default +0x88BC response at +0x8974 was corrected against instructions:
widths above four return 0xFFFFFFFF; width four returns 0xFF; the remaining
default cases return 0xFFFF. The separately handled expansion range returns
zero. Focused assertions live in `test_native_emit.c`.

The continuation also exposed a rehost-specific link problem. The original
+0x2888 SWL updates 24 target bits because PRX and cache share the upper
target bits on PSP. Our relocated helper offsets are base-zero, so preserving
those bits redirected a repeated call to 01B8071C instead of 09B8071C. The
native bridge now replaces all 26 JAL target bits while retaining its opcode.
This is an explicit base-zero adaptation, not a byte-identical PSP patch.

`out/ffvi_run.OHz3BH/result/` reads expansion address 1F000084, specializes a
BIOS byte read, reuses linked blocks and reaches BFC06784. It stops while
compiling a branch with a load delay slot at +0x61B0, after 29,427 generated
instruction-hook observations and 15 block-entry transfers. FFVI has not booted.
