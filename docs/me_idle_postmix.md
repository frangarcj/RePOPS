# ME idle postmix and native state layouts

The reached active callback now returns a packed sample after its 24-voice
loop. This increment reconstructs the idle-CD, disabled-reverb-write and fixed
master-volume paths. It does not complete the original callback or claim that
the host is playing audible game music.

## Function recovery

`src/native/pops_spu.c` follows the original postmix from +0x288. On the reached
idle-CD path it preserves the notification reset and zeroes both CD capture
slots. Parameter changes rebuild the reverb work-area base, coefficient set and
both channels' tap addresses (+0x434..+0x54C).

With reverb writes disabled, the original still reads old taps, computes the
comb/allpass tail, advances addresses with wrapping and updates its three-value
history. `advance_reverb_tail` follows this route through +0xC84, +0x65C and
+0x924; it does not replace disabled reverb with a state reset. The compatibility
flag can suppress the new tail sample while preserving those state changes.

The fixed master-volume path scales and publishes the effective gains, clamps
and packs the output, advances the capture cursor and updates IRQ/status fields.
The caller's packed result is written only after this path completes. Streaming
CD (+0x2A0), enabled reverb writes (+0x5AC) and volume-sweep setup/stepping remain
explicit boundaries, alongside earlier incomplete envelope and voice cases.

## Types used by actual C

`src/native/pops_state.h` describes the recovered wire fields and provides field
addresses and a named core clock accessor. New postmix code uses members such as
`master_volume.right.level`, `reverb_parameters.comb_gain`, `reverb_channels`
and `irq_cursor`, not scattered numeric field offsets. `rp_core_guest_cycles(c)`
expresses the core event deadline minus remaining cycles.

The C layout types are descriptors, never cast onto guest backing. `offsetof`
selects the field and the existing little-endian memory functions perform the
read/write; host pointer size, alignment and module/scratchpad aliasing do not
change the guest representation. Static assertions check the relevant extents.
Earlier anonymous fields remain debt to resolve when that area is touched, not
a reason to write new known fields anonymously or start another global rewrite.

The matching Ghidra schema now names `MasterVolumeState` (0x18),
`ReverbParameters` (0x14), and `ReverbChannelState` (0x40), and places them inside
the mixer prefix. New shared and CD fields are named where the postmix exposes
their use. The importer supports signed 32-bit fields. A fresh import validated
16 root types and exported `out/postmix-types.1kF9Iy/export/repops_types.gdt`.
No existing Ghidra project or original firmware bytes were modified.

## Focused checks

`make native test-native-spu test-native-me` passes. The SPU test retains the
earlier release/ADPCM/interpolation checks and verifies reverb address advancement,
capture advancement, master-volume publication and an actual packed nonzero
synthetic output: `0x007C01F2` (left 498, right 124). The reached real startup
samples are zero; the synthetic fixture is not evidence of FFVI music.

The test also checks field addresses independently, unsigned clock subtraction
across wrap and an explicit CD-stream boundary that leaves the output untouched.
These are host contract checks, not exhaustive binary equivalence or on-device
validation. Disabled reverb behavior is not proof that its enabled-write path
works. The callback remains one partial entry in the function ledger.

## Runtime handoff

The first integrated run beyond postmix exposed a separate host scheduling
problem: the cooperative ME was not advanced during SPUSTAT polling. That fix
and the subsequent complete diagnostic are described in `me_poll_scheduling.md`.
The observation counts there measure executed generated cache activity, not
function-recovery percentages.
