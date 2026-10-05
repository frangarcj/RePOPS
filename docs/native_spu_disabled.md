# First native POPS sample-callback branch

`src/native/pops_spu.c` reconstructs the SPU-disabled path at POPS offset zero:
entry -> +0x1968 -> +0x9D4, ending at +0xA64. The original instruction ranges
were inspected with Capstone alongside the Allegrex listing. The function
updates the producer marker, counter, notification, ring index, status and
shared words. The enabled-to-disabled transition clears the observed voice
state and flags. The returned word follows S1, including a nonzero carried
value when present; it is not an unconditional silence stub.

The native callback refuses SPU-enabled or producer-busy input before changing
producer memory. That guard limits the implemented domain; it does not model
the original busy loop or concurrency. Cache operations are elided because
both C components share one host backing store. Active mixing, ADPCM, envelopes,
noise, reverb and real audio-device scheduling are not reconstructed here.

The FFVI diagnostic reaches this branch after the actual reset C initializes
SPU state. Its returned word is zero for that initial state. The worker then
generates ACK 1, returns to POPS reset, receives control 0 and generates ACK 0.
Neither acknowledgement is inserted by the harness. Device ready/status
responses remain an explicit headless sink adaptation.

Checked runs:

- `out/ffvi_run.v7wWNW/result/`: normal optimized diagnostic build.
- `out/ffvi_run.tLHkUJ/result/`: integrated diagnostic with ASan and UBSan,
  no sanitizer findings in this run.
- `out/ffvi_run.grsiAk/result/`: normal non-bypass path still stops at UI +0x28DF8.

The diagnostic stops at graphics initialization +0x1B9C4, with 83 instrumented
calls including repeats. All runs retain `game_executed: false`. The new
`make test-native-spu` is a small synthetic smoke check (carried return word,
disable transition, refusal of active state), not an equivalence proof.

Next execution boundary is +0x1B9C4 and its GE/data-table setup. The larger
remaining CPU task is the point where POPS produces and jumps to MIPS game
code: compiling the generator as host C does not make emitted MIPS executable
on this Mac.
