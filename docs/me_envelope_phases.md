# Media Engine envelope threshold transitions

The callback's table at module +0xD4074 maps phases 4..24 to the branches
+0x14C8..+0x16E0. The native callback now reconstructs each nontrivial table
entry: attack setup, key-on repeat reset, attack knee, decay, four sustain
modes, release setup and terminal stop. Other table entries retain their
original no-op transition behavior.

`rp_envelope_phase` names the recovered phase values. The 16-byte envelope
and 0x74-byte voice layouts are used by native code. The four threshold
halfwords at mixer +0x850 are named in the C layout and JSON map, and the
reset producer now initializes them through that same layout.

The implementation retains the original unsigned rate rotation and halfword
period truncation, including zero-period disabling. Rising exponential phases
share +0x153C's knee adjustment. A transition sets the next rate without
replacing the current level. The caller preserves the distinction between the
clamped stored level and the reported halfword on a threshold crossing.

Thirteen focused native-callback vectors pass under AddressSanitizer and UBSan.
They cover attack setup, both knees, decay, sustain selection, release modes,
zero-period handling, terminal stop and key-on repeat clearing. Existing SPU
and ME worker checks also pass. These are contract checks informed by the
original instructions, not a whole-callback equivalence proof or an exhaustive
ADSR-domain test.

`out/me-phases.uYbFq9/result/` advances beyond the old +0x14C0 boundary and
stops at DMA callback +0x9364. It records 39,469,825 generated-cache instruction
observations and 40,346 entry transfers; `game_executed` remains false.
This extends the existing partial callback at offset zero and does not add
new original functions to the coverage numerator.
