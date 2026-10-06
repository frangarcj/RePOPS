# Enabled reverb, POPS callback +0x550..+0x924

The native callback now shares one reverb implementation for enabled and
disabled writes. The recovered `rp_reverb_parameters_layout` and
`rp_reverb_channel_layout` remain the wire descriptions; no guest memory is
cast to a host struct.

Enabled processing runs the two IIR updates, the four comb taps and the two
allpass stages before the existing output-history filters. IIR reads the old
write cursor and writes its successor, wrapping at the sound-RAM end. Allpass
stages write the current cursor. Reads remain ordered after preceding writes,
including when a later tap points to the same address. Disabled processing
continues advancing cursors without those writes.

Wet inputs are the accumulated voice contributions, selected by capture phase
and muted with the original control bit. Idle CD and fixed master gain remain
the supported postmix domain; streaming CD, volume sweeps and other envelope
transitions remain separate boundaries.

## Focused verification

`make native test-native-spu test-native-me` passes. The SPU fixture checks a
nonzero voice contribution through reverb input/IIR, and both phases with writes
enabled/disabled. An alias fixture produces IIR samples 1000 and 250, then
allpass samples 25 and 200 and history value 125. APF2 reads the freshly written
25 instead of the old 99. A wrapping IIR destination also updates the IRQ latch.

`out/reverb-vertical.nvPi1C/result/run.json` records the integrated next boundary
at CPU helper +0x1DE8, after 26,738,259 hook observations and 2,643 block-entry
transfers. These checks do not prove hardware timing, general binary equivalence,
audible output or FFVI boot. `game_executed` remains false.

Evidence: the pinned original listing +0x550..+0x924, +0xC84..+0xC9C and the
voice accumulators +0x244..+0x268. The callback still counts as one partial
original function, not a collection of newly completed firmware functions.
