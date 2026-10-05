# Initial BIOS flow and state emission

The `--emit-flow` probe now reaches the end of the first analyzed BIOS region.
It adds the forward jump over empty records, the zero-operand known ALU path,
CPU status writes through +0x46A0, and exit-target emission through +0x6768.
The helpers reproduce POPS output; they do not provide an ARM backend.

The forward J at PS1 PC 0xBFC00070 targets 0xBFC00150. Because the intervening
records are empty after the delay slot, POPS emits a cycle debit but no jump
word for this case. Later the register-zeroing OR instructions use the original
constant tracking, including the FPR-backed locations. The MTC0 Status record
uses the original 0xF27DFF3F mask and the following exit record emits the
original cache-table/slow-target sequence.

```sh
.tools/verify-env/bin/python scripts/probe_cpu_analysis.py \
  --out out/cpu_flow_new --emit-flow --compare
```

`out/cpu_flow_resume_02/comparison.json` matches all 348 generated Allegrex
bytes, the 49,168-byte record buffer and the entire 16 KiB scratchpad.
The existing emitter smoke test also passes with sanitizers.

This is still a selected-record probe, not the complete +0x58C0 controller.
The controller must also assign output addresses to records, flush at joins,
schedule delay slots and patch links. General ALU paths, conditional/backward
jumps and non-status state writes remain explicit boundaries. No generated
Allegrex instruction or PS1 instruction was executed by this probe.
