# Media Engine reconstruction workstream

The Media Engine (ME) is explicitly included in RePops. The objective is to
recover its relevant behavior as native C in the Mac harness, not just replace
registration calls with success values or run the original code under an
emulator. The current disc path continues independently; ME remains a required
integration milestone before claiming sound or full POPS execution.

## What is already available, and what is not

`src/me_registration.c`, `src/me_startup.c` and `src/popsman_mailbox.c` model
bounded provider behavior. Their isolated tests do not reconstruct the code
that produces samples. The startup comparisons supply scripted service/device
responses and must retain that qualification.

`ghidra/AnalyzeMeCallback.java` seeds POPS offset zero. The earlier read-only
pass at `out/media_engine_01/pops_me_callback.c` produced warning-bearing C and
non-contiguous recovered ranges. It is analysis material, not a native mixer.
`data/me_reverse_targets.json` records initial analysis windows, deliberately
not a claim that all bytes are code or that function discovery is complete.

## Follow the complete execution chain

1. **POPS caller:** at +0x1A038, A0 is the module-relative-zero callback and A1
   is 0x09FF8000. Recover the preceding state initialization too. Offset zero
   is not a null runtime pointer once the module is loaded.
2. **Registration and bootstrap:** provider +0x3490 patches stack immediates
   in the +0x2F28 template and stores the callback. +0x35D8 copies 0x60 bytes
   to 0xBFC00040 and starts the handshake. Existing models cover this boundary,
   not the processors' physical reset/cache behavior.
3. **ME loop:** provider +0x2F88 loads the callback slot at +0x4C5C. The call
   at +0x3124 consumes the returned word as signed halfwords. Recover every
   control path, including the indirect halfword read, drain/fade and stop.
4. **Callback/sample producer:** recover POPS +0x0000 and its reachable code,
   data tables and state updates. Establish the sample packing and arithmetic
   contracts from instructions, rather than translating Ghidra's types blindly.
5. **Native integration:** run the reconstructed callback from a cooperative
   host ME worker using one shared state model. Only acknowledge actual state
   transitions. A later host PCM adapter must preserve the recovered packing,
   ordering and rate; no silent audio-success stub.

These offsets are module-relative, and POPS and POPSMAN are separate images.
The provider read at +0x4C5C must never resolve into the consumer's memory just
because both analysis databases use image base zero.

## Shared memory and I/O to reverse

| Address / region | Evidence and remaining question |
| --- | --- |
| 0xBFC007F8 | Control request; bit tests select distinct loop paths. Do not treat all values as Boolean. |
| 0xBFC007F0 | At provider +0x31A8 the loop writes the control value as acknowledgement. It is not a generic success flag. |
| 0xBFC007F4 | Written by C93C56F8 after an unsigned shift by five; the ME loop consumes it. The interpretation must remain separate from the numeric contract. |
| 0xBE000070 | The loop writes packed output here. Device buffering and timing remain unvalidated. |
| 0x09F40000 / 0x49F40000 families | Callback accesses paired address families and cache instructions. Recover aliases, ownership, widths and synchronization before sharing host buffers. |
| 0x09FF0858 and following state | Accessed by the callback; exact structures and lifetime remain to be recovered. |
| POPSMAN +0x4C5C, +0x4C68 | Callback slot and startup state, belonging to the provider image. |

These are evidence-backed starting points from the pinned listings and existing
Ghidra exports, not a finished hardware map. Use address/width/access/caller
records to connect writers and readers. A native test with no concurrency
cannot establish the PSP cache-coherency protocol.

## Firmware variants and possible dependencies

Use the current corpus POPSMAN hash beginning `ff4222e8` for new integration.
Earlier tests used the modified ARK provider beginning `83ed5373`. The audit
compares the relevant raw windows but does not infer equivalent module state
or relocation behavior just from equal instruction bytes.

`kd/me_wrapper.prx` exists in the corpus. Audit imports and actual callers to
decide whether it or an auxiliary ME image is reached on the POPS-specific
path. Its presence or library names alone are insufficient. Do not add every
module containing the letters `me` to the dependency set.

## Acceptance gates and next work

- [x] Add the explicit ME scope, evidence windows and reproducible inventory.
- [ ] Compare relocated provider addresses/state across the corpus and ARK
  references before carrying older models into the native runtime.
- [ ] Recover callback CFG, indirect destinations, tables and shared-state
  reads/writes; retain gaps and decompiler warnings.
- [ ] Reconstruct one callback iteration with explicit unsigned wrapping,
  signed shifts, saturation and sample packing where observed.
- [ ] Compare returned samples **and state writes** against an appropriate
  instruction oracle on captured/constructed valid states. Unsupported cache
  and device behavior must be outside the claimed test scope.
- [ ] Integrate the callback and request/ack loop into the native harness.
- [ ] Produce checked PCM from real execution before claiming sound works.

Progress on these gates is independent of decompiler success counts. A
complete startup test is not a complete ME reverse, and a generated C file is
not a verified function.

## Reproduce the window audit

```sh
python3 scripts/audit_me_targets.py --out out/me_target_audit_new
```

The audit reads local hash-pinned inputs and emits only JSON measurements in
a new output directory. It neither executes the firmware nor dumps its bytes
into Git. Further Ghidra work must use read-only processing or a fresh project,
as required by AGENTS.md.
