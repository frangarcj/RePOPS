# Cached stack accesses and BIOS function returns

The +0x3CA8..+0x3FE4 path of +0x3A90 is now represented in C. It keeps the
translated address in A0, tracks the displacement at GP+0x748, and preserves
the original branch-likely RAM/scratchpad choice. FPR-backed loads/stores and
paired unaligned accesses retain their emitted MIPS encodings.

The +0x42A4 helper records link locations and fills delay slots. Forward
non-fallthrough jumps use the existing final linker, while backward jumps
retain the cycle handoff. The controller now captures indirect targets
before their delay slot and handles constant conditional branches. Original
PRX instructions still do not execute in Unicorn.

`out/ffvi_run.kujVtA/result/` compiles the BFC06EC4 region into 648 bytes and
executes it, including calls to BFC01A60/BFC03990 and an indirect return.
It reaches an unreconstructed byte-read helper at +0x1A90 after 29,339
generated-code hook observations and 11 block-entry transfers. The last
recorded guest block PC is BFC06ED4; this is not a precise per-instruction PC.
There is still no game execution, rendered image or active-voice audio.

`make test-native-emit` adds focused stack-base reuse, FPR load and indirect
link assertions. The integrated run exercises these paths but is not a full
original-vs-C equivalence test. Rare load/nested-delay hazards remain explicit.
