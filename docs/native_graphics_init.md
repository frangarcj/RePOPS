# Native graphics initialization prefix

The FFVI diagnostic `out/ffvi_run.nzXeFC/result/` now passes the former
+0x1B9C4 blocker up to the first display refresh call at +0x115B4.
No framebuffer is rendered and no PS1 instruction is executed.

`src/native/pops_graphics.c` reconstructs the observed 4-MiB shadow-EDRAM path:
texture/view descriptors, palette values, command and vertex templates, the
initial GPU control reset, display choice and background value. It uses the
same data copied from the locally hash-checked relocated POPS image, not new
compatibility tables. Numeric function/command addresses are never executed
as host pointers.

Two startup lists are retained in order in `rp_context.ge_commands`. This is
a headless command-capture adapter, not a GE interpreter or renderer. The
first pending draw list is queued at its initial stall address. Draw and
control-flow commands are refused by the bounded startup-list capture; no
draw is marked as rendered. The GE synchronization adapter means prior state
commands have been captured, not that a PSP device has completed work.

RAM aliases 0x48xxxxxx and shadow EDRAM aliases 0x44000000..0x443FFFFF share
their corresponding host storage. This does not implement cache coherency or
EDRAM hardware swizzling. The requested translation value 0x200 is retained
as metadata. POPSMAN's +0x2270 wrapper calls an impose service; its return is
unused at this call site and the native headless adaptation is logged.

The +0x11410 prefix computes the initial delay from original float constants,
rounds to nearest/even, inserts its event with reconstructed +0x945C, and calls
the reconstructed interrupt-bit setter +0x96E4. For this input the scheduled
delay is 49,509 guest-cycle units and the callback entry is +0x1265C. Scheduling
an event is not executing the scheduler or displaying a frame. Timer gating
currently covers the disabled gate, and GPU port writes cover the initial
control reset command only.

Checks: compiler warnings treated as errors, integrated FFVI diagnostic run,
and existing focused regressions. No new exhaustive verifier was introduced.
The UI bypass remains explicit; the normal path still stops at +0x28DF8.
Original firmware and game files remain unchanged and excluded from commits.
