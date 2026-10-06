# BIOS wait observed before the CD completion deadline

The end of out/cd-wait.4NdwzQ/result/trace.jsonl records:

- Guest cycle observation: 195,928,620.
- CD current sector in the seek state: 18.
- Seek deadline: 210,670,940.
- Secondary response deadline: 210,687,324; event predecessor is nonzero.
- CD interrupt flags: 0; secondary pending response code: 2.
- CPU status/cause: 0x40000401 / 0; I_STAT/I_MASK: 1 / 12.

Thus the secondary reply remains queued 14,758,704 cycles beyond the observed
clock. The host dispatch budget expires before this response becomes due.
This is evidence of an outstanding timed seek, not proof of a lost completion
or of an infinite BIOS loop. Dispatch and BIOS consumption of that later
response still need an integrated observation.

The loop in the supplied embedded BIOS alternates BFC07DDC and BFC07E00.
Its preceding calls read the two event handles stored at A000B9BC/A000B9C8
and invoke the BFC0D9E0 wrapper for B0 service 0x0B. The executed RAM entries
0xB0 and 0x1EC8 recur in the trace. No BIOS event value has been patched.

The new diagnostics use the existing named CD/CPU/IRQ layouts, record the
seek helper's selected delay and append a small state summary only at the
host limit. They do not change guest state, add a reconstructed body or claim
an emulator framebuffer. The integrated result is 164,943,156 generated-code
observations and 820,186 compiled-entry transfers; game_executed remains false.

The process interval in run.json is 86.918 seconds with the no-timer bridge.
Host load and initial clock-dependent state were not held constant against
older runs; do not turn this into a controlled whole-emulator speed ratio.
