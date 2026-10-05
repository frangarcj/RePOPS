# Memory-card worker startup

The diagnostic path now runs the startup prefix of POPS +0x1AA90 in C.
It binds two slots, formats fresh volatile cards, checks their free blocks,
updates the ready flag and writes -1 to module +0x14CC64 before parking at
the semaphore boundary. The wait in +0x1A908 now finishes through its producer.

The historical ARK listing is not a byte oracle: at +0x1ABC8 it shows an SP
store, while the pinned ELF actually has SW T5 with T5=-1. Use the pinned
ELF and its relocated Ghidra image for the reconstructed handoff.

This uses a deliberately selected RAM-only storage adapter on
`REPOPS_DIAGNOSTIC_SKIP_UI=1`. It does not read or overwrite user cards and
does not implement VMP authentication, savedata UI, or dirty-card writeback.
It must not be treated as a persistent memory-card backend.

`make test-native-memory-card` checks sector checksums, slot pointers, free
blocks and the handoff with sanitizers. The integrated run
`out/ffvi_run.tQ4ouw/result/` reaches the next boundary at +0x1A00:
the published Allegrex code cannot yet execute on the native host.

Normal startup still stops at the unreconstructed PSP UI. No PS1 execution
or gameplay is claimed by this step.
