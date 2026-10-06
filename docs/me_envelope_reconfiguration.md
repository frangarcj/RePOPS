# Voice dirty flags and ADSR reconfiguration

The original +0x180C compares the shared ADSR word with the voice's cached
configuration. On equality its taken delay slot clears the current voice's
dirty sign before envelope processing. A pitch, volume or repeat write is
therefore not sufficient to request envelope reconfiguration.

The native callback previously treated every dirty voice as an ADSR change
and stopped at +0x16E4. It now tracks the comparison before storing a changed
configuration. Key-on and newly started release still bypass that reconfiguration
path, preserving their original control order.

The +0x16E4..+0x1704 transition is also reconstructed: countdown becomes one,
the current level becomes the threshold, phase becomes `(phase & ~3) - 4`,
and exponential/step are cleared before entering the existing envelope update.
Other unresolved phase transitions retain explicit boundaries.

The 16-byte envelope and 0x74-byte voice are now native wire layouts in
`pops_state.h`. The existing DSP offsets derive from those fields, and the
configuration comparison uses the typed shared/private voice records.

Focused SPU checks distinguish unchanged ADSR (countdown continues, phase and
level preserved) from changed ADSR (transition fields and level preserved).
Existing nonzero sample, reverb ordering/wrap and ME-worker checks still pass.
This is not full ADSR-domain, timing or instruction-equivalence validation.

The integrated result in `out/me-envelope.MIfAP7/result/` passes the
reconfiguration and reaches phase 20 in the threshold-transition table at
+0x14C0. It records 39,468,863 generated-cache observations and 40,333 entry
transfers; `game_executed` remains false. The next work is that actual phase
transition, not another forced sample or ready response.
