# Active Media Engine callback: first control prefix

The native worker now selects `rp_pops_spu_sample`, which preserves the existing
complete disabled-SPU branch and starts a separate partial enabled branch.
It does not invoke Unicorn for the original callback. This is reviewed C for
POPS offset zero, not another emulated firmware execution path.

## Recovered active prefix

The prefix follows +0x0000..+0x0184: it acquires/releases the shared producer
flag, increments the callback counter, snapshots and consumes dirty/key/repeat
masks, updates the IRQ cursor and runs the noise phase/count feedback update.
It then follows the first voice's reached control paths:

- +0x17F0: pitch, repeat address, ADSR words and fixed-volume state.
  Volume sweeps still stop at their original branch entries.
- +0x1774: key-on start address, optional paired-voice state, predictor and
  envelope initialization.
- +0x1708: simultaneous key-off starts release, preserving its period/step
  and exponential-mode fields.

These paths execute in the original order for voice zero. We do not pre-apply
changes to all 24 voices, because the original processes each voice's sample
before advancing to the next voice.

## Exact remaining boundary

The actual run has dirty mask 80FFFFFF, key-on mask 00FFFFFF and key-off mask
FFFFFFFF. It reaches the release initialization of voice zero and stops at
`ME_voice_sample_path_not_reconstructed`, +0x11CC. The enabled callback has NOT
completed, no packed sample is returned, and later voices have not been mixed.
The stopped invocation is an analysis checkpoint, not a resumable partial
sample contract. Reverb, ADPCM decoding, envelope evolution, volume sweeps and
active output are still pending.

`out/ffvi_run.vVQcDk/result/` records the native ME path from the SPUCNT=C010
write. The generated CPU-cache counters stay at 9,580,458 observations and
1,688 entry transfers while the ME prefix progresses. This is not an unchanged
CPU blocker: the native audio callback now advances into first-voice state.
The run remains headless/diagnostic with `game_executed: false`.

## Checks and evidence

`make test-native-spu test-native-me` preserves the disabled-path checks and
adds an enabled-prefix fixture that checks mask consumption, start address,
fixed volume and release state. It also asserts that the prefix stops and does
not replace the caller's output word with an invented sample. The callback
remains one partial original function in the progress ledger.

Evidence is the hash-pinned original listing at +0x0000..+0x01E4,
+0x1708..+0x1968 and the already reconstructed disabled tail. These are reviewed
contract tests and a host run, not exhaustive binary equivalence or PSP audio
timing validation. The cooperative ME sink remains a diagnostic adapter.
