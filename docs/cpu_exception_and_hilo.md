# CPU emitter: shift pairs, HI/LO and exception records

The reconstructed `+0x6914` record emitter now includes:

- SLL/SRA pairs that become byte or halfword sign extension, and right/left
  pairs that clear low bits with INS. The second record is elided and known
  values are updated. The separate SLL/SRL-to-EXT path is still pending.
- Multiply/divide records routed through the original ALU/register allocation
  path, with the original live/dirty HI/LO flags at GP+0x751/0x752.
- MFHI/MFLO and MTHI/MTLO records, including memory-backed versus live hardware
  values and dirty-state spills. These are generated Allegrex operations;
  Unicorn is the execution backend, not a second interpreter in this project.
- Exception records that store the guest EPC, debit cycles, write the cause
  fields and call the reconstructed exception-vector helper at +0x94C4.

Basis: the local hash-pinned POPS disassembly and Ghidra output for +0x6914,
including its calls to +0x31D0, +0x2E9C, +0x4504 and +0x46A0. Names here are
recovered descriptions, not original Sony symbols.

`make test-native-emit` exercises representative sign-extension, DIV and MFLO
encodings and HI/LO state transitions, in addition to the previous tests.
These are focused contract tests, not exhaustive binary equivalence. Integrated
runs proceed into BIOS RAM routines and decompression; FFVI is not yet booted.
The overall original emitter remains marked partial in function_progress.csv.
