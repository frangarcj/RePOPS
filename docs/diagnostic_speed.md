# Host diagnostic overhead

The generated-code bridge now passes zero for Unicorn's per-call wall-clock
timeout by default. Its 10,000,000-instruction bound, explicit native-helper
exits, generated-code checks and global run_native.py timeout are retained.
REPOPS_UNICORN_TIMER=1 restores the earlier 10-second per-call timer. This
changes host overhead, not generated instructions or guest cycle accounting.

The local 5,000-entry experiment in out/unicorn-timer.wuPO9h/result.json
checks registers and exits in both configurations. The three no-timer runs
take 0.049--0.063 seconds; the timed runs take 0.613--0.667 seconds. This is
roughly 12x for that micro-operation, not an emulator speedup claim.

out/fast-run.VdKwon/result/run.json records 154.081 seconds of native process
execution at the 2,000,000-dispatch diagnostic limit. The earlier long run did
not measure that same interval. Their final guest counters differ, so they
are not a controlled whole-run equivalence or speed-ratio measurement.
The generated-cache contract tests pass both with and without the timer.

The Python runner streams to the last nonempty trace line rather than loading
and parsing every event into memory. Three focused tests cover missing/empty
traces, a terminal result after many records and refusal of a truncated last
record. The report records native process wall time separately from the
configured timeout. These changes reconstruct no additional POPS function.
