# First disabled-display refresh

The native diagnostic now returns from the reset path of +0x115B4 rather than
stopping at entry. It reconstructs the idle-UI prefix, display-mode state,
disabled-screen GE command sequence and the frame bookkeeping. Active display,
TV, non-idle menus, dirty texture-cache relocation and repeat-frame paths still
stop explicitly. This is a supported path, not the complete renderer.

The headless host captures display-buffer/mode requests, supplies no pending
HOME/power events, advances a virtual VBlank to the requested first frame and
records the GE stall release. No real-time synchronization or rendering is
claimed. Seven words are appended at 0x49A00014; the original command-buffer
reset and guest-state writes are retained.

The run `out/ffvi_run.79kAPn/result/` reaches the following GPU-handler
installation at +0x1BA98. The existing diagnostic option bypasses startup UI
+0x28DF8 and remains required. The normal path is unchanged.

```sh
REPOPS_DIAGNOSTIC_SKIP_UI=1 ./run_ffvi.sh
```

Evidence: the pinned +0x115B4, +0x1B7C8, +0x34350 and +0x30C24 disassembly and
Ghidra outputs. Validation in this phase is a host run, not a binary comparison
of display/device timing. FFVI and generated Allegrex code are not executed.
