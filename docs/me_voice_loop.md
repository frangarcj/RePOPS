# ME voice loop: recovered path and remaining boundaries

The active callback at POPS module offset zero now reaches +0x288, immediately
after the loop over 24 mixer voices. It is still a partial callback: the final
CD/reverb/output path does not return a packed sample. Original firmware is not
executed for this callback; the reconstructed C runs inside the cooperative ME
worker.

## Recovered behavior

The previously reconstructed mailbox, noise and per-voice controls now feed:

- +0x1418..+0x1484: envelope countdown and linear/exponential update, including
  threshold crossing. Terminal release/sustain cases converge through +0x16D8
  and +0x1670. Other phase transitions stop at +0x14C0.
- +0x1278..+0x139C: ADPCM block address/flags, repeat address, three-sample history
  preservation, two signed predictor terms, nibble expansion and saturation.
  Each block supplies 28 samples. IRQ-overlap handling at +0x13A0 is pending.
- +0x11DC..+0x1274: bounded pitch, refill, four-coefficient interpolation,
  noise selection, envelope multiplication and position update.
- +0x0204..+0x0284: fixed-volume accumulation, voice-1/voice-3 capture writes,
  per-voice volume publication and advancement by the original 0x74-byte stride.

No controls are pre-applied to later voices: the callback preserves the original
control -> envelope -> sample -> accumulation ordering for each voice. The
reverb mask rotates where the original sample path rotates it. Cache operations
remain host-coherent memory adaptations, not a PSP timing model.

## Actual diagnostic

Run `REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh`. The recorded run is
`out/ffvi_run.ZeJEMV/result/`. It enters this callback during the existing
SPUCNT=C010 write, processes all 24 voices and stops with:

```
ME_24_voice_loop_complete
ME_post_voice_mix_not_reconstructed @ 0x288
game_executed: false
```

The pending key-on and key-off masks immediately put these voices into release;
their accumulated contributions are zero. This is evidence for the reached
initialization path, not audible game music or full SPU accuracy.

## Checks

`make native test-native-spu test-native-me` passes. The SPU smoke checks retain
the disabled callback and now cover terminal release, a filter-zero block with
known nonzero nibbles, a nonzero interpolation/capture fixture and the explicit
post-voice boundary. The caller's output word remains untouched when the
incomplete active callback stops. These are focused host contract tests, not
exhaustive instruction-level equivalence or hardware validation.

Pending behavior includes attack/decay and other envelope transitions, volume
sweeps, ADPCM IRQ-overlap quirks, first-voice pitch modulation and the final
CD/reverb/output section. No further functionality should be added during the
requested structure-recovery pass. The ledger keeps offset zero as one partial
function, regardless of the number of samples, voices or helper routines.

## Evidence

The hash-pinned `build/pops_660_reference.s` supplies the listed instruction
ranges. `src/native/pops_spu.c` contains the reconstruction and recovered voice
offset names. `src/native/pops_reset.c` initializes the coefficient tables and
the mixer/shared-memory areas. The phase-target table at module +0xD4074 maps
states 17, 19 and 24 to +0x16D8, and state 16 to +0x1670; other transitions are
not silently treated as equivalent to release.
