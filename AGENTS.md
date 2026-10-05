# RePops analysis conventions

- Current goal: reconstruct the PSP POPS-related PRX functions in C and execute
  that C natively in a macOS harness. Not a Vita port, not a new PSP PRX target,
  and not merely running the original firmware inside another emulator.
- JPCSP may supply reference semantics and a comparison oracle. Original PRX
  files are analysis inputs. Measure progress by the native C execution path.
- Preserve earlier hybrid-PRX/isolated-test experiments, but prioritize an
  integrated executable over perfect byte matching or exhaustive leaf tests.
- Media Engine code is explicitly in scope: recover the POPS callback, the
  POPSMAN ME loop and shared-memory protocol, then integrate reconstructed C.
  Startup/mailbox mocks do not count as a reconstructed mixer or ME runtime.
  Check whether me_wrapper or auxiliary firmware is actually reached before
  treating it as a dependency. See docs/media_engine_reverse.md.
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
- Prioritize moving the integrated native execution path forward. Use focused
  build/smoke checks; defer exhaustive hardening and extra test infrastructure.
- Primary deliverable is the reconstructed POPS emulator. A CPU backend or
  provisional execution bridge is support work to avoid blocking that reverse,
  not a separate optimization/recompiler project. Preserve original behavior
  and distinguish temporary execution adapters from reconstructed functions.
- Maintain data/function_progress.csv when reconstructing a function. Count
  original functions once per module/profile and entry, not calls, helpers or
  decompiler outputs. Separate complete bodies, partial paths, adapters and
  pending work; record the actual verification scope and native integration.
