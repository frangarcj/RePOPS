# Initial reverse-engineering findings

Target: PSP 6.60 `pops_01g.prx` / module name `pops`.

This document separates observed facts from working hypotheses. Addresses are
module-relative virtual addresses in the decrypted PRX.

## Binary and function map

Observed:

- repacked PRX: 521,564 bytes; the `DADADAF0` tag does not establish Sony encryption
- decrypted ELF: 1,134,970 bytes
- ELF entry / `module_start`: `0x00016000`
- export table begins at `0x0003D4A4`
- import descriptors occupy `0x0003D4BC..0x0003D690`
- 159 imported functions across 23 PSP libraries (the original scanner missed
  the final five-word `UtilsForUser` descriptor)
- the `scePopsMan` library supplies one import, `scePopsManExitVSHKernel`,
  but the POPSMAN module also supplies the 29 `sceMeAudio` imports
- historical ARK listing: 656 subroutine entries
- independent Capstone seeds: 721, covering 656/656 historical entries
- Ghidra Allegrex analysis: 695 entries, covering 652/656 historical entries

The 43 Ghidra-only entries are worth auditing rather than automatically
discarding. At least three are already independently justified:
`0x0000D1B0` is a genuine alternate entry used by the PEOPS POPS hooks,
`0x00016080` is the `popsmain` thread entry passed as a function pointer, and
`0x00024998` is the target installed by POPS' own syscall-stub patcher. None was
marked as a normal subroutine by the older listing.

## POPSMAN -> POPS boundary

Observed in the 6.60 POPSMAN disassembly:

1. POPSMAN calls `sceKernelGetModel()`.
2. It formats `flash0:/kd/pops_%02dg.prx`.
3. It calls `sceKernelLoadModuleForKernel(...)`.
4. It starts POPS with:
   `sceKernelStartModule(modid, 0, NULL, NULL, NULL)`.

Therefore POPS has no private argument block passed by POPSMAN at module start.
This does not eliminate the runtime contract: the manager also provides
`sceMeAudio`, and the versioned metadata join finds 30 imports supplied by
POPSMAN. Shared memory and initialization side effects still need auditing.

## Boot flow

### 0x00016000 - module_start

High confidence. Ghidra shows that it:

- sets the compiled SDK/compiler version;
- puts the display into hold mode;
- creates a thread named `popsmain` with entry `0x00016080`;
- starts that thread.

### 0x00016080 - popsMainThread

High confidence. This is an indirect function pointer passed to
`sceKernelCreateThread`, so ordinary call-target discovery misses it unless it
is seeded explicitly.

The current top-level sequence is:

1. setup logging/build string and power callbacks;
2. perform a Memory Stick `sceIoDevctl`;
3. call `0x00024A08`, the syscall-stub patch installer;
4. initialize/open the PS1 image and start CD worker state;
5. initialize a large PAF/UI resource block;
6. initialize/switch disc state;
7. create the memory-card worker;
8. initialize controller state;
9. perform additional emulator/hardware setup;
10. enter the core path at `0x00001A00`.

## Host syscall bridge

### 0x00024A08 - syscall stub patch installer

High-confidence behavioral description.

The function searches for PSP import stubs matching the normal
`jr $ra ; syscall N` shape. It allocates/copies a table, then rewrites the
original stubs into jumps to `0x00024998`, placing a per-stub index in
`$v0`. It flushes/invalidate caches after patching.

### 0x00024998 - patchedSyscallBridge

High confidence. This entry has no normal prologue and is reached by the
self-modified stubs, so static function discovery commonly misses it.

The Allegrex decompile shows:

- save interrupt mask;
- disable interrupts;
- calculate the original/copy stub address using the index in `$v0`;
- inspect the caller's two instructions;
- either jump to the selected original stub or patch the caller in place;
- flush I/D caches;
- restore the interrupt mask.

This is a significant part of POPS' host integration and needs to be understood
before any native rehost.

## CD image path

### 0x0001B2F0

High confidence: PS1 image/container setup.

Evidence:

- reads `PSTITLEIMG000000` and `PSISOIMG0000` headers;
- supports up to five disc offsets;
- creates event flag `cdread`;
- creates and starts thread `cdworker`.

### 0x0001B004

High confidence: disc selection/reload path for `PSISOIMG0000`.

It updates per-disc offsets, parses the selected image header and rebuilds
sector/file metadata.

### 0x000083E8 - cdrTransferSector

Known from the PEOPS hook and independently coherent in Ghidra. It queues sector
buffers into a small ring in the `0x49F402xx` area and synchronizes with the
audio/media side.

### 0x0000D1B0 - cdrWriteRegister

Known from the PEOPS hook. It is an alternate entry into a larger CD command
dispatcher and selects handlers through a 16-entry jump table.

## SPU path

### 0x00007F00 - spuWriteRegister

Known from the PEOPS hook.

The function normalizes byte/halfword/word writes and ultimately writes PS1 SPU
register values into the `0x49F40000` region. It also maintains queue/control
state in nearby `0x49F402xx` addresses.

This region and the heavy use of `sceMeAudio` imports show that PSP POPS
delegates significant audio work to the Media Engine side rather than
implementing all SPU output as ordinary main-CPU software.

## Input and memory cards

### 0x0001A950

Very high confidence: memory-card worker initialization.

It creates semaphore `mcWriteBack`, creates thread `mcworker`, and starts it.

### 0x0001A57C

Very high confidence: controller/input initialization.

It configures sampling mode, idle-cancel thresholds and controller state, then
sets the sampling cycle.

## PS1 CPU execution core

### 0x0001C254

Observed initialization for the execution component below. Its role as the
main PS1 CPU core has **not** been established.

It clears a state area rooted near `0x00010000`, initializes multiple
subsystems/callbacks and constructs status/lookup data used by the executor.

### 0x0001E20C

Observed: executor-like code consuming 16-bit words. The previous description
as the main PS1 instruction/IR executor was premature.

Evidence from Ghidra:

- guest-like register state is rooted around `0x00010000`;
- it fetches **16-bit** operation words;
- the top bits select operation families;
- suboperations implement AND/XOR/OR/NOT, add/subtract, compare, multiply,
  loads/stores and address-space dispatch;
- it routes memory accesses by guest address region to dedicated handlers;
- it tracks flags, exceptions and cycle-like accounting.

The word width alone does not identify the guest ISA or prove that this is a
translated R3000A stream. In the recovered bootstrap it is reached conditionally
through `0x0001BF30`, after `0x0001C964`, before the main path at `0x00001A00`.
Recover the producer, entry conditions and input format before deciding whether
this is a CPU backend, a helper VM or another startup mechanism.

### 0x0001BF30

Strong hypothesis: executor startup/run wrapper.

It initializes execution state through `0x0001C254`, then repeatedly enters
`0x0001E20C` with different control values and waits on an emulated status
condition.

## Host UI / PAF

### 0x00028DF8

High confidence: large PSP-host UI/resource initialization rather than the
minimal PS1 CPU core.

It is dominated by `scePaf` calls, resource structures, image/font/layout
state and host display assets. For a native Vita rehost this is a strong
candidate for replacement rather than source-level reconstruction.

## External dependency surface

Import counts:

| Library | Functions |
| --- | ---: |
| scePaf | 35 |
| sceMeAudio | 29 |
| ThreadManForUser | 15 |
| UtilsForUser | 5 |
| IoFileMgrForUser | 14 |
| sceLibFont_HV | 11 |
| sceGe_user | 8 |
| sceVshCommonUtil | 6 |
| sceCtrl | 5 |
| sceDisplay | 5 |
| sceUtility | 4 |
| InterruptManager | 3 |
| SysMemUserForUser | 3 |
| scePower | 3 |
| Kernel_Library | 2 |
| pspvmc | 2 |
| sceImpose | 2 |
| sceRtc | 2 |
| LoadExecForUser | 1 |
| sceDmac | 1 |
| sceOpenPSID | 1 |
| scePopsMan | 1 |
| sceUtility_private | 1 |

The concentration is useful: a rehost does not need to emulate an arbitrary PSP
process. Most external calls fall into a few replaceable groups: UI/PAF,
Media Engine audio, threading, I/O, GE/display, input and memory card services.

## Next reverse targets

1. Identify the producer and purpose of the 16-bit stream consumed around
   `0x0001E20C`; do not assume it represents PS1 instructions.
2. Name the memory-region handlers called by the executor.
3. Recover the host syscall bridge table around `0x00024A08/0x00024998`.
4. Separate PS1 GPU command handling from PSP GE presentation.
5. Map the `sceMeAudio` calls to the SPU/CDDA/XA pipeline.
6. Recover the structures rooted at `0x00010000` (CPU state) and
   `0x49F40000` (SPU/media shared state).
