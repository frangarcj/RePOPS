# RePops analysis conventions

- Target the hash recorded in README.md; do not silently mix firmware versions.
- Keep firmware binaries, reference disassembly, Ghidra databases and bulk
  decompiler output out of Git. Do not publish them implicitly.
- `out/decompiled/` is raw pseudocode. `src/` contains reviewed reconstruction
  models. A compilable model is not automatically binary-equivalent or a port.
- Mark names, boundaries, subsystem identities and structures as hypotheses
  unless supported by concrete instructions, callers, imports or runtime data.
- An import library name is not the same thing as its provider PRX module.
- Use the Allegrex Ghidra extension and inspect relocation results. Capstone's
  generic MIPS decoder does not fully model Allegrex/VFPU.
- Do not overwrite existing Ghidra projects or export directories. Prefer
  read-only processing and new result directories.
- Preserve decompiler warnings and failed functions in generated indexes.
- Run `python3 -m unittest discover -s tests -v` after changing the parser.
- Test the bootstrap model with the C11 compiler/sanitizer command in README.md.
- Report contract tests separately from instruction-level equivalence tests;
  no full-emulator or on-device validation has been performed yet.
- Save completed, tested work in small semantic commits as it progresses.
  Stage explicit source paths only; never include firmware or bulk outputs.
