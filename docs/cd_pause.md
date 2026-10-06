# CD Pause: reached command path

Target: pinned PSP POPS 6.60. Original command dispatcher +0xAE5C,
Pause instructions +0xB3C0..+0xB4C4 and common response tail +0xB2E8.

The native command uses the existing CD layout. It records the saved flag,
checks lid/seek state, schedules the primary response after 0x4000 cycles,
and chooses the completion delay from the sector or secondary-event deadline.
The minimum is 0x5880 when idle; active delivery uses 0x5EF4C or 0xB9E99,
multiplied by six unless the original timing flag disables that multiplier.
Audio synchronization precedes clearing the track and cancelling delivery.
The second reply remains an event, not an immediate successful response.

`make native test-native-cdrom` passes. The focused fixture checks event
cancellation, drive flags, distinct pending IRQ kinds and both deadlines.
These are host contract checks, not all Pause inputs or hardware timing.

Integrated result: `out/cd-pause.3eV9ZD/result/run.json`, with the source
and data hashes recorded by the runner. The first sector had already reached
RAM via DMA. Pause then progresses to GPU reader +0x12FBC with 39,246,306
generated-cache observations and 38,997 entry transfers. No FFVI boot,
rendered frame or complete CD controller is claimed.
