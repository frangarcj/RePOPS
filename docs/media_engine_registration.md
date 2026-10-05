# Media Engine: registration contract and startup boundary

## Resume audit

The workspace had progressed beyond the initial scaffolding. Before editing,
both firmware hashes were checked, the 14 existing synthetic Python tests and
the 36 bootstrap / 24 mailbox C contract cases were rerun successfully. There
was no active Ghidra analysis process. The repository still had no commits;
existing files were untracked, not an empty project. Nothing was reset, staged,
published, or removed.

The earlier `out/media_engine_01/` already contained a recovered POPS callback,
696 Ghidra function entries in that in-memory pass (one more than the earlier
695-entry export), and bounded relocated-code exports. These are retained.

A fresh read-only Ghidra export went to
`out/media_engine_resume.9RXA5b/`. Its 15,968-byte provider range was identical
to the previous export. The original Ghidra database was not changed.

## What the caller actually supplies

At POPS offset `0x1A038`, the call to `sceMeAudio_DE630CD2` supplies:

- A0: the entry at **POPS module-relative offset zero**.
- A1: stack value `0x09FF8000`, formed in the call's delay slot.

Offset zero in a relocated module is not a null runtime pointer. The recovered
callback is separately addressed code, not the application's `module_start`
at `0x16000`. The existing Ghidra callback output is raw pseudocode, with
warnings and non-contiguous recovered body ranges, not a verified mixer.

Sources: the pinned consumer, `build/pops_660_reference.s` around the call,
and `out/media_engine_01/consumer_setup/`.

## Reconstructed routine: provider +0x3490..+0x3513

`src/me_registration.c` models the 132-byte registration routine. The provider
is the hash-pinned ARK 6.60-derived reference from `popsman_contract.md`, not
an authenticated pristine Sony module.

1. Compute `mask = K1 << 11`, truncated to 32 bits.
2. Reject when bit 31 of `mask & entry` or `mask & stack` is set. Return
   `0x80000023`, restore K1 and perform no module-state writes.
3. Read the words at module offsets `0x2F2C` and `0x2F30`.
4. Store the callback entry at module offset `0x4C5C`.
5. OR the low 16 stack bits into the word at `0x2F30` and store it.
6. OR the high 16 stack bits into the word at `0x2F2C` and store it, in the
   original JAL's delay slot.
7. Call the helper at `0x35D8`, with shifted K1 in effect. When it returns,
   restore K1 and return zero regardless of its V0.

The two patched words are the bootstrap's `LUI SP` / `ORI SP,SP` pair.
This is **OR**, not replacement of an immediate field. Registering stack
`0x09FF8000` and then `0x00010001` without restoring the template produces
`0x09FF8001` in that pair. The model preserves this behavior; it does not
silently make repeated registration behave like a clean setter.

The routine does not validate stack alignment or reject a zero entry as part
of this observed check. Later startup code has other preconditions. Passing
the registration check is not evidence the supplied callback can execute.

Ghidra's raw C reordered the state assignments. The reconstruction follows
the observed instruction order instead, and tests compare that order.

## Startup helper: statically traced, not hardware-validated

At `0x35D8`, the provider calls reset/clock and interrupt services, updates
`0xBC100070`, and copies the `0x60`-byte bootstrap template from `0x2F28`
to `0xBFC00040`. It combines the first copied word with the low two bits of
module state at `0x4A08`, performs cache/DDR operations, and initializes the
mailboxes:

- `0xBFC007F8` gets state from `0x4C68` when bit 1 is set, otherwise `1`.
- `0xBFC007F0` is set to `0` before the ME reset-disable service.
- It polls `0xBFC007F0` until it equals `1`, calling the delay service with
  `100` while waiting. The inspected loop has no timeout.

The bootstrap sets SP using the patched pair, performs privileged/cache
operations and transfers control to provider offset `0x2F88`. That loop reads
the callback from `0x4C5C` and invokes it repeatedly. The returned V0 is split
into two signed 16-bit values in a path feeding `0xBE000070`. This supports
an audio-sample interpretation; the exact hardware protocol is not established
by our tests. `0xBFC007F8` also selects paths involving an indirect halfword
read, draining/fading, acknowledgement, and wait states.

These observations are from the pinned listing and Ghidra's
`000035D8_FUN_000035d8.c` / `00002F88_FUN_00002f88.c`. Do not collapse them into
a generic 'start thread' replacement or treat every acknowledgement as a
success response. Real reset timing, cache coherence, device flags and the
producer/consumer memory ordering remain unvalidated.

## Differential test scope

`scripts/verify_me_registration.py` checks the source ELF hash, the Ghidra
manifest, and the exact relocated export hash before executing anything.
All 172 words changed from the raw provider range correspond to APPLIED
relocation records. Within the registration routine, only the word at
`0x34F0` changes; its store offset resolves the callback slot to `0x4C5C`.
The base-zero relocated registration slice has SHA-256
`28a7feca561f768ee8245c58604a564ac90f745d49951c1fc41d0af8c85d6874`.

Unicorn 2.1.4 executes the unchanged relocated registration instructions via
the KSEG0 code alias. The helper's first 16 bytes are explicitly replaced in
the **test memory only** with `LUI V0; ORI V0; JR RA; NOP`, returning a
controlled value. This is a mock at the helper boundary, not an emulation of
Media Engine startup. No original file or saved Ghidra database is patched.

For each native C build (`-O0`, `-O2`), the suite runs 320 boundary cases,
1,024 deterministic random cases and 32 consecutive state-preserving calls:
**1,376 per build, 2,752 comparisons**. Each build exercises 894 accepted calls
and 482 rejections for seed 660. Checks cover ordered reads/writes, the helper
boundary snapshot, return value, full mapped module memory, K1, SP, RA and the
selected callee-preserved GPRs. A budget and terminal-PC check reject incomplete
execution. Guest-stack scratch contents and physical devices are excluded.

The runner initially exposed a tool limitation: directly redirecting PC or
modifying registers inside a MIPS code hook did not safely model a return.
The final runner observes hooks only and uses a normal return stub. In this
build, the hook fires twice consecutively at the helper boundary. It folds
only adjacent identical boundary snapshots with no intervening instruction;
it records the count (894 per build) and rejects later re-entry or changed
snapshots. A pre-instruction hook is not an instruction-retirement trace.
This nuance and the stub mean these are bounded behavioral comparisons, not
a formal proof or a test of instruction timing. Earlier failed reports are
preserved alongside the passing report, not overwritten.

There are 24 additional C contract cases under AddressSanitizer/UBSan and six
synthetic tests of the harness's vectors and refusal guards. They are separate
from the instruction-execution comparisons.

The final run is in
`out/media_engine_resume.9RXA5b/verification_final/verification.json`.
The previous three leaf routines also passed all 3,252 cases again under
`out/media_engine_resume.9RXA5b/mailbox_regression/verification.json`.
Final regressions: 20 synthetic Python tests and 84 C contract cases (36
bootstrap, 24 mailbox, 24 registration), all passing. Contract-case counts
must not be conflated with instruction-execution comparisons.

## Reproduction

The previous Ghidra projects and exports must remain intact. Choose a new
directory each time; both the exporter and test runner refuse existing outputs.

```sh
ROOT=$PWD
RESULT=out/media_engine_new
mkdir "$RESULT"
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
"$GHIDRA_HOME/support/analyzeHeadless" \
  "$(cat out/popsman_ghidra_project_path.txt)" popsman \
  -process f0-kd-popsman.prx -readOnly -noanalysis \
  -scriptPath "$ROOT/ghidra" \
  -postScript ExportMemoryRange.java "$ROOT/$RESULT/provider_code" 0 0x3e60 \
  > "$RESULT/ghidra.log" 2>&1
.tools/verify-env/bin/python scripts/verify_me_registration.py \
  --relocated "$RESULT/provider_code" --out "$RESULT/verification"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address \
  src/me_registration.c tests/test_me_registration.c -o build/test_me_registration
./build/test_me_registration
python3 -m unittest discover -s tests -v
```

The pinned optional test environment uses `unicorn==2.1.4`. The exported
range is loader-adjusted memory, not an assertion that generic MIPS Capstone
decoding replaces Allegrex relocation support.

## Next boundary

Trace and test the actual request/acknowledgement protocol and sample-loop
memory accesses without presuming hardware success. Registration is now a
small, isolated contract. The ME loop, mixer, reset helper, GPU and CPU engine
are not consequently reconstructed. No PSP/Vita boot or full-emulator build
has been performed.
