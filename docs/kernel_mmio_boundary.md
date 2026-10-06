# Observed kernel and MMIO boundary

This is a bounded inventory, not a declaration that all POPSMAN dependencies
are recovered. Provider instruction offsets below are from the hash-pinned
ARK-derived listing `build/popsman.s`; its provenance is in popsman_contract.md.
Keep the corpus and ARK provider profiles distinct.

## Graphics provider +0x3A00

The sceMeAudio_7014C540 provider executes MFIC/MTIC, reads CP0 EPC and performs
cache operations. It writes GE stall address 0xBD40010C, polls command status
0xBD400304 with mask 0x4, and writes mask 0x4 to 0xBD400310 on one return path. The other
path uses GE enqueue/list-sync imports after restoring interrupt state.

Provider +0x3ACC (sceMeAudio_E7F06E2B) writes argument & 0x1FFFFFFF to
0xBD40010C. Neither service is a sound-only operation despite its library name.

JPCSP's MMIOHandlerGe maps +0x10C to stall, +0x304 to command status and
+0x310 to changeCmdStatus. Its change operation is not simply named an IRQ ACK;
the mask changes command status and related interrupt state. Do not replace
the whole +0x3A00 service with one guessed high-level call.

## ME boot provider +0x35D8

Observed calls include sceSysreg_driver NIDs 0AE8E549, 457FEBA9, 38527743,
48F1C4AD, sceKernelCpuSuspendIntr, sceKernelCpuResumeIntrWithSync,
sceKernelDcacheWritebackInvalidateAll and sceDdrFlush(4).

The helper reads 0xBC100070, clears mask 0x4 (bit 2) and writes it back while interrupts
are protected. It copies 0x60 bootstrap bytes to 0xBFC00040, updates the
control/acknowledgement memory and releases the ME reset path. Delays and
memory copies in this function are not themselves evidence of privileged
instructions; the whole low-level boot context is the relevant boundary.

## ME worker +0x2F88

| Addresses observed | Classification or observed use |
| --- | --- |
| BC000000/004/008/00C/040/044 | Memory-access-control configuration |
| BC200000 and BC200004 | CPU/bus frequency registers, not DMA |
| BC300008 | Written with 0x1000 in worker initialization |
| BE000000..BE000050 | Audio device setup and status polling |
| BE000070 | Packed output-sample write |

The exact accesses and ordering are preserved in src/native/me_worker.c.
CPU_BUS_FREQUENCY replaces the former ambiguous TRANSFER constant; this is
a naming correction only. It does not add clock, cache or interrupt emulation.

## Memory is not automatically device MMIO

BFC007F8 is the control request, BFC007F0 is acknowledgement and BFC007F4
is a parameter consumed by the worker. BFC00040 holds the copied bootstrap.
These are memory locations used by the protocol, not all independent hardware
registers. Their physical availability and privilege context still matter.

Similarly the PS1 addresses 1F801810/14 are guest GPU ports interpreted by
POPS, not hardware PSP MMIO. Model reads/writes to these must not be confused
with native accesses to BD400xxx.

## Prospective user-mode PSP backend

The user proposed a later homebrew target for PPSSPP, not a change of the
current native-Mac target. Preserve the original GE-list boundary and adapt
the low-level provider effects through appropriate user APIs. Execute the
reconstructed mixer under a host/PSP-user scheduler rather than assuming
PPSSPP will run the original ME bootstrap or PSP kernel.

A separate blocker is memory layout: current POPS graphics setup requests
4 MiB EDRAM, while the inspected PPSSPP sceGeEdramGetSize returns 2 MiB.
Returning a fictitious size is not a solution. Generated Allegrex blocks also
need a real PSP ABI/address bridge in place of the Mac's Unicorn helper exits.

## Primary references inspected

- Local POPSMAN listing: +0x35D8..+0x36D7, +0x3A00..+0x3AD8.
- Local models: src/me_startup.c and src/native/me_worker.c.
- jpcsp/jpcsp, src/jpcsp/memory/mmio/MMIO.java, blob
  945312fa6fe6925bafdb81631e9694cc203993d0: block classification and BFC RAM.
- jpcsp/jpcsp, src/jpcsp/memory/mmio/MMIOHandlerGe.java, blob
  63cfae0ed9f483781ed3c430f941e285e09fbcb3: register map and command status.
- hrydgard/ppsspp, Core/HLE/sceGe.cpp, blob
  67d8dca6f757b0fa380c8d6f3ee34fac1777a693: user GE calls and EDRAM size.

Upstream material was inspected on 2026-10-06. These are source observations,
not validation on PSP hardware or proof that the proposed homebrew boots.
