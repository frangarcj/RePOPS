# POPSMAN reference: measured provider boundary

## Inputs and provenance

The consumer is the POPS reference already pinned by README. The provider is
ARK-3 `contrib/PSP/f0-kd-popsman.prx` at commit
`4197ef8555e13ca757b5a6e11fec69e6c0f0d96a`:

- File bytes: 21,192.
- SHA-256: `83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855`.
- Git blob: `eb8b07bb7c2b89dd281e5327fe4c3a7377ee7358`.
- Historical listing: `contrib/PSP/popsman.s` at the same commit;
  SHA-256 `6f4ab6a1bbf2f22cbec82cffa37689a5e964631abb3e25e33d9f839cd462996d`.

This is a **6.60-derived ARK reference, not an authenticated pristine Sony
firmware image**. Its real export table names `noAudio` and `noAudio_driver`,
where the previously used version metadata names `sceAudio` and
`sceAudio_driver`. Preserve that distinction; do not generalize all behavior
to stock firmware. The two libraries used for the 30-service join are
`scePopsMan` and `sceMeAudio`.

## Parsing and analysis results

POPSMAN sets bit 31 in its first PT_LOAD `p_paddr`. It is a kernel flag, not
part of the module-info file offset. The parser now masks it, with a synthetic
regression test. The export descriptor parser separates function entries from
variables and rejects truncated/reversed/overrunning descriptor ranges.

The provider export table contains 56 function entries and two variables,
across seven libraries; 34 of the functions belong to `sceMeAudio`.
Joining consumer imports and provider exports by **both library and NID**
produces 30 matches: 29 `sceMeAudio` and one `scePopsMan`.

Fresh Ghidra analysis used `Allegrex:LE:32:default`, image base zero and a
separate project. It exported 160 function entries, all producing pseudocode,
with 12 warning-bearing outputs and 852 relocations marked APPLIED. These
counts include wrappers/import stubs and do not prove function completeness
or correctness of the pseudocode.

The report `out/popsman_contract_01/contract.json` joins the measured provider
addresses to the consumer stubs and the existing Ghidra static caller sets.
An empty caller set is **not evidence that an import is unused**: indirect
calls, unresolved control flow and incomplete function recovery remain.

## Important correction: sceMeAudio is not a sound-only interface

The named library contains host-facing services for several subsystems. These
are investigation categories, not recovered Sony names:

| NID | Provider offset | Direct observation |
| --- | --- | --- |
| `2AC64C3F` | `0x23AC` | Calls GE EDRAM set-size with `0x400000`, then get-size. |
| `7014C540` | `0x3A00` | GE MMIO, cache and interrupt operations; a fallback calls GE enqueue/list-sync. |
| `E7F06E2B` | `0x3ACC` | Masks the argument and writes `0xBD40010C`. |
| `30BE34E4` | `0x23D0` | Calls file seek and conditionally file read. |
| `0FA28FE6` | `0x1888` | Calls file seek. |
| `805D1205` | `0x1950` | Calls file close. |
| `14447BA0` | `0x050C` | Calls BBMac services. |
| `D4F17F54` | `0x2244` | Wraps an impose service while preserving K1. |
| `8A8DFE17` | `0x2594` | Wraps power unlock while preserving K1. |
| `DE630CD2` | `0x3490` | Checks two arguments, stores an entry value at module offset `0x4C5C`, patches two immediates near `0x2F28`, calls `0x35D8`. |
| `68C55F4C` | `0x3514` | Writes `0xBFC007F8`, polls `0xBFC007F0`, uses codec/delay services. |

In particular, do not propagate a guessed `SetPause` name to `7014C540` just
from a cross-version NID chain: this reference contains a graphics path.
Nor does starting POPS with zero arguments make it a platform-independent
library. The imports still expose display, files, privileged state and the
Media Engine.

## Three reviewed leaf contracts

The C models are in `src/popsman_mailbox.c`. Guest addresses are numbers
passed to a host callback, never dereferenced as native pointers.

### C93C56F8, offsets 0x3590..0x35AB (28 bytes)

Writes the unsigned argument shifted right by five to `0xBFC007F4`, executes
SYNC, returns zero. There is no clamping in the observed instructions. A
volume-control interpretation is plausible but the tested contract is only
the numeric transform/write/barrier/return sequence.

### 0BABD960, offsets 0x35AC..0x35D7 (44 bytes)

Computes `(K1 << 11) & argument` in 32 bits. If bit 31 is set, returns
`0x80000023` without writing. Otherwise writes `argument + 2` modulo 2^32 to
`0xBFC007F8` and returns zero. The semantic type of that argument is not
assumed; the model is not a generic pointer-validation API.

### E7F06E2B, offsets 0x3ACC..0x3ADB (16 bytes)

Writes `argument & 0x1FFFFFFF` to `0xBD40010C` in the return branch's delay
slot. No return-value contract is assigned to the C model.

## Differential validation

`scripts/verify_mailbox.py` reads original instructions directly from the
hash-checked ELF. It executes these 88 bytes of code in Unicorn 2.1.4 using a
MIPS32 24KF model; all selected instructions are shared MIPS32/r2 operations.
It separately compiles our C to a native host shared library and compares
observations. No original opcodes are embedded in the test source.

For each routine the test checks 60 boundary pairs and 1,024 deterministic
random argument/K1 pairs (seed 660): **1,084 cases per routine, 3,252 total**.
All passed in the first run, stored in
`out/mailbox_verification_01/verification.json`.

Checked: defined returns for two routines, ordered writes, presence/order of
SYNC, final contents of both mapped memory pages and unchanged K1. Addresses
are normalized for MIPS KSEG1's uncached physical alias. An exact terminal PC
and instruction budget reject runaway/incomplete execution.

Not checked: real PSP device responses, physical synchronization between
processors, timing, interrupts, complete ABI equivalence, every possible
input, or Vita execution. SYNC is recorded as an instruction event, not
validated as a hardware memory-ordering guarantee.

There are also 24 C callback-contract cases, passing with AddressSanitizer
and UBSan. They are separate from the binary-execution checks.

A second fresh Ghidra project, produced with `run_popsman_ghidra.sh`, exported
160 C files byte-identical to the first pass, again with 852 applied
relocations and 12 warning-bearing files. Regenerating the raw-table contract
from this second index also produced an identical JSON report. The existing
output-directory guards for the mapper and differential harness were tested
and refused to overwrite earlier results.

## Reproduce without overwriting earlier work

```sh
sh scripts/run_popsman_ghidra.sh build/f0-kd-popsman.prx out/decompiled/popsman_second
python3 scripts/map_popsman.py \
  --popsman-index out/decompiled/popsman_second/index.json \
  --out out/popsman_contract_second
python3 -m venv --system-site-packages .tools/verify-env
.tools/verify-env/bin/python -m pip install unicorn==2.1.4
.tools/verify-env/bin/python scripts/verify_mailbox.py --out out/mailbox_verification_second
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address \
  src/popsman_mailbox.c tests/test_popsman_mailbox.c -o build/test_popsman_mailbox
./build/test_popsman_mailbox
```

All source binaries, Ghidra databases and bulk C outputs remain excluded from
Git. This phase does not produce a working POPS rehost or Vita emulator.

Next substantial boundary: the Media Engine entry/stack setup and the
`0xBFC007F0/7F4/7F8` handshake, followed by the GE submission path. Both need
producer/consumer analysis and device-aware validation, not just leaf tests.
