# Native single-disc header path

## Measured advancement

The supplied FFVI PBP now passes the previous `sceMeAudio_14447BA0` blocker
through an explicitly limited native adapter. The complete 0xB3C80-byte header
is present at guest address 0x09E80000: 1,024 bytes from the earlier read plus
735,360 bytes from PSAR+0x400. The normalized identifier is `SCES03828`.

The next real blocker is POPS +0x24770, which consumes the identifier, header
version words at +0x420/+0x424, and configuration data at +0x428. No success
has been fabricated for that routine. The selected-disc active byte is not
updated yet; the original does that only after configuration succeeds.

Current ordinary run: `out/ffvi_run.c93OG9/result/`. Current ASan/UBSan run:
`out/native_disc_sanitized.OZywdU/`. Both stop at +0x24770. The ordinary trace
records 15 instrumented C function entries and 14 host-service calls; those
numbers include prefixes/adapters, not fully recovered functions. PS1 CPU,
graphics, PCM output and gameplay remain absent.

## Evidence and reconstructed effects

The provider used here is the extracted corpus `kd/popsman.prx`, SHA-256
`ff4222e8085190e1f357aece096282963e0e8e317b76547fafb1600a746a237e`.
Fresh Ghidra results are under `out/disc_header.kFr4nt/`. The seeded read-only
pass recovers +0x050C and +0x0D3C without overwriting earlier databases.
The first attempt under the hidden Codexify project path failed Ghidra's path
validation and was retained; the successful project uses a fresh `/tmp` path.

At +0x050C, the provider asks +0x0D3C to read 0xB3880 bytes at disc-relative
+0x400. On the single-disc field-update path it records values from +0x12B4,
+0x12B8, +0x1220 and +0x1224. The native adapter preserves the observed 32-bit
addition and the carry into the high word of the +0x1220 field. Its later
semantic use is not established; it is called `offset_1220`, not assumed to
equal the physical EOF.

The return is derived from the first DATA.PSP word XOR 0x4A08B53F, not a
hardcoded zero. For this file the DATA.PSP word is the ELF magic 0x464C457F,
so the return is 0x0C44F040. POPS +0x28730 stores its first result using the
observed second XOR and leaves later stored state untouched.

POPS +0x1B61C removes underscores/hyphens. The original assembly sets V0 to
A0 and preserves it as the return pointer, although raw Ghidra output had a
`void` return type. The caller at +0x1B0E0 uses that pointer. The C signature
therefore returns it. +0x1AF90 then checks four uppercase letters and five
digits. Assembly and call-site inspection, not the inferred C type, decide
this contract.

## Explicit boundaries, not hidden stubs

`src/native/pops_disc.c` implements **only the plain single-disc adapter**.
Admission requires a bare-ELF DATA.PSP word and a structurally supported ID
field after a complete read. These are format checks, not authentication or
proof that arbitrary remainder bytes are meaningful.

Protected/unknown DATA.PSP, protected/unknown header fields and multi-disc
requests stop explicitly. The original protected-file open/IOCTL operations,
K1 checks and BBMac branch are not implemented or falsely reported successful.
Malformed/truncated PBP component ranges are rejected. A bounded normalizer
also refuses an unterminated field instead of scanning beyond its 32 bytes.
These host safety guards intentionally restrict the supported input domain.

The provider's BC100040 low-bit read is recorded as `deferred_hardware`; no
PSP mode value is guessed. That value feeds ME bootstrap in the original,
so the native ME integration must resolve it through an explicit host policy
or a recovered contract before relying on it. Header loading alone is not
ME initialization. See `media_engine_reverse.md` for that workstream.

The adapter stores recovered fields in a separate `rp_disc_header_state`,
not at POPSMAN offsets inside the consumer's base-zero analysis image.

## Validation and reproduction

```sh
make test-native-disc
python3 -m unittest discover -s tests -v
python3 scripts/inspect_disc_header.py input/games/final_fantasy_vi/EBOOT.PBP
./run_ffvi.sh
```

The C tests construct their own temporary file. They check complete and short
reads, field arithmetic including carry, format rejection, multi-disc refusal,
address overflow, provider-result storage, normalization, pointer return,
character validation and unterminated-field rejection. ASan/UBSan pass.
The full native FFVI path and scratchpad/module-memory regression also pass
with sanitizers. These are host integration/contract tests, not a proof of
original instruction equivalence or cryptographic/device compatibility.

Next native step: recover +0x24770's per-game table/configuration path using
the actual GP-relative accesses. ME callback state and sample production are
a separate required workstream before claiming sound.
