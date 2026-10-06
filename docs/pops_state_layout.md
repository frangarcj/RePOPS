# POPS state layout: first structural reverse pass

Target: POPS 6.60, SHA-256
`6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0`.
This pass names layouts; it does not add emulator functionality or claim the
original source used these C types. Evidence is the hash-pinned original listing,
its relocated data image and the referenced native reconstruction. Machine-readable
field definitions are in `data/state_layout.json`.

## 1. Resolve the base before naming a field

`gp + offset` is not a universal reference to one global structure.

| Context | Base | Evidence and interpretation |
| --- | --- | --- |
| Main CPU/core | `gp = 0x00010000` | Scratchpad state. `runtime.c:rp_memory` maps 0x10000..0x13FFF to separate storage. The same numeric module offsets refer to different bytes and require `rp_module_memory`. |
| ME shared producer state | `gp = 0x49F40000` | Callback +0x000C loads this base. Its cached/physical alias is 0x09F40000, loaded into `fp` at +0x0018. They are one backing store, not two independent structs. |
| ME mixer-private state | `ra = 0x09FF0000` | Callback +0x0010 repurposes RA as a data base; the caller's RA was saved on the stack. Voices begin at base+0x858. |
| Alternate GP context | `gp = 0x09FF8000` | Caller +0x1BF40/+0x1BF44 sets this base before +0x1C254 initializes its distinct prefix. Own list links and mask table are confirmed below. Subsystem ownership and full extent remain unresolved. |
| Temporary register use | varies | E.g. +0xE054 sets GP to -1. A GP-based load must be classified by incoming register state, not by register name alone. |

The source-level hypothesis is several state blocks/substructures and selected
register-base optimizations, not a proven monolithic Sony `PopsState` class.

### Alternate context: a concrete decompiler attribution error

The raw pseudocode for +0x1C254 displays a clear of absolute address 0x10000.
The actual instruction at +0x1C258 is `move a0,gp`: the initializer uses its
incoming GP. The +0x1BF40 caller sets that to 0x09FF8000, not core scratchpad.
Treating the displayed absolute address as evidence would merge unrelated state.

The initializer clears 0xB04 bytes, self-links pointers at +0xACC/+0xAD0, and
builds 16 u32 nibble-to-byte masks at +0x17C..+0x1BB. Its callee +0x1C3B8
writes a byte at +0xFC, where the core context has a saved return word. These
observations support a separate context, not a demonstrated mirror. The new
`AlternateGpPrefix` describes only the cleared prefix; +0x1C600..+0x1C610 also
uses GP+0xB04, so 0xB04 is not claimed as the full allocation size.

## 2. The concrete example: GP+0x35F0

This is the callback field of a 16-byte event at core GP+0x35E4:

| Core GP offset | Relative offset | Width | Recovered field |
| --- | --- | --- | --- |
| 0x35E4 | +0x00 | 32 | `frame_phase.event.next` |
| 0x35E8 | +0x04 | 32 | `frame_phase.event.prev` |
| 0x35EC | +0x08 | 32 | `frame_phase.event.deadline_cycles` |
| 0x35F0 | +0x0C | 32 | `frame_phase.event.callback` |
| 0x35F4 | +0x10 | 32 | `frame_phase.remaining_delay` |

+0x11410 installs callback +0x1265C. +0x1265C can replace it with +0x15FE4
for the early-phase path. +0x15F54 switches it back to +0x11410. These are
changes of the same callback field, not evidence of unrelated structures.
See `pops_graphics.c:begin_frame`, `advance_frame_phase`, `finish_frame_phase`.

`PopsEvent` is supported by three independent consumers: insertion +0x945C,
dispatch +0x953C and removal +0x9668. The decoder reads 32-bit guest addresses;
`callback` is NOT a native macOS function pointer. The base-zero harness uses
module-relative numeric targets where appropriate.

Other event instances: GP+0x35D0 (callback at +0x35DC, extra quota at +0x35E0)
and GP+0x35F8 (callback +0x3604). Three timer objects also embed the event at
relative offset zero.

## 3. Main core fields and bounded arrays

These are confirmed field roles, not a claim that all gaps have been recovered.

| GP offset/range | Suggested name | Evidence |
| --- | --- | --- |
| +0xFC | `helper_saved_return` | Memory helpers and +0x2888 save RA here. Do not blindly label the whole first 256 bytes as a dense GTE register array. |
| +0x130 | `cpu.status` | Status writes +0x46A0 and exception preparation +0x94C4. |
| +0x134 | `cpu.cause` | +0x94C4 and IRQ write +0x98C4. |
| +0x138 | `cpu.exception_pc` | Exception emission in +0x6914 and +0x953C. |
| +0x180..+0x19F | `saved_guest_register_slots` | Register reload +0x29C4 and slot mapping. Not a 32-element contiguous GPR array. |
| +0x1A0 | `cpu.resume_pc` | Dispatcher +0x2650, link helper +0x2888. |
| +0x1A4 / +0x1A8 | `cpu.saved_hi` / `cpu.saved_lo` | +0x2D3C flushes the host HI/LO values here. |
| +0x1AC / +0x1B0 | `events.deadline` / `events.downcount` | Current guest time is deadline minus downcount; comparison and wrap behavior matter. |
| +0x1B4 | `events.resume_code` | +0x1A68/+0x953C handoff. Guest code address, not PS1 PC. |
| +0x1B8 / +0x1BC | `events.head.next` / `events.head.prev` | Intrusive list sentinel links. See overlap warning below. |
| +0x1C0 (16 bits) | `core.control` | Negative/reset and nonzero/cache-invalidation paths in +0x953C. |
| +0x1C3 / +0x1C4 / +0x1C8 | `irq_poll.step`, `previous_time`, `current_time` | Polling acceleration in +0x9850. |
| +0x1CC / +0x1D0 | `code_cache.ram_cursor` / `bios_cursor` | Compiler preparation/publication and +0x7E60. |
| +0x1D4 / +0x1D8 | `ram_exception_entry` / `ram_page_mask` | +0x94C4, +0x58C0 and +0x7E60. |
| +0x1E0 | `cpu.cache_control` | Default memory writer +0x89A0. |
| +0x64C, stride 0x20, count 3 | `timers[3]` | Reset +0x1A3A8 and register writer +0x9C60. |
| +0x6AC onward | `configuration_and_boot_fields` | Mixed game flags, timing overrides and disc state; no complete struct boundary yet. |
| +0x740..+0x797 | `register_allocator` | Temporary names, dirty bits, host locations and static register map. |
| +0xB40..+0xBDB | `compiler_analysis` | Mode/cost/source mapping and 32 known constants. |
| +0x1000, stride 8, count 512 | `io_handlers[512]` | Read/write target pair selected by the mapped eight-byte guest I/O slot. |
| +0x2000..+0x2FFF | `io_register_shadow` | Selected direct register values; +0x2070/+0x2074 are IRQ status/mask. Not all words have the same type or width. |
| +0x3000..+0x33FF | `ps1_scratchpad_shadow` | Memory helpers translate the PS1 scratchpad here. |
| +0x3400 onward | `graphics_state` | Texture/cache entries, events, GE cursor, GPU registers and flags. Full extent and aliases remain open. |

### Timer object (0x20 bytes)

`event` +0x00..+0x0F; `target_with_flags` u32 +0x10; `origin_cycles` u32
+0x14; `mode_with_status` u32 +0x18; `irq_bit` u8 +0x1C; `clock_shift` u8
+0x1D; +0x1E..+0x1F unresolved. `target_with_flags` is not merely an unsigned
count: bit 31 can represent the stopped state. See +0x9B6C/+0x9A54/+0x9C60.

### Compiler analysis block

+0xB40 u16 mode, +0xB42 u16 baseline cost, +0xB44 u32 pending cost,
+0xB48 u32 source-address bias, +0xB4C u32 record high-water address,
+0xB50 u32 source PC, +0xB54 u32 source limit, +0xB58 u32 known-register mask,
+0xB5C u32 known values[32]. The mask uses `0x80000000 >> guest_register`.

## 4. ME structures recovered from strides and paired accesses

### Shared voice registers: 16 bytes, 24 entries

Base `0x49F40000`, stride 0x10. Offsets +0x00/+0x02 are raw left/right volume;
+0x04 pitch; +0x06 start address units; +0x08/+0x0A ADSR words; +0x0C current
reported envelope; +0x0E repeat address units. All are 16-bit fields. Keep raw
volume control words separate from the signed effective levels below.

The shared block also contains key/dirty mailboxes and capture data. Important
fields are +0x280 pending key-on, +0x284 pending key-off, +0x288 dirty mask,
+0x294 callback count and +0x29E producer/consumer handshake fields. Mixed byte
and halfword accesses at +0x29C..+0x29F require a protocol-aware overlay.
Sample RAM begins at shared+0x2C0; it is not part of a voice object.

### Mixer voice: 0x74 bytes, 24 entries

Base `0x09FF0858`, stride 0x74. The 24-entry extent is exactly 0xAE0 bytes,
matching reset clearing at +0x198C..+0x199C.

| Voice offset | Size | Suggested member |
| --- | --- | --- |
| +0x00 | 10 | `left_sweep` |
| +0x0A | 10 | `right_sweep` |
| +0x14 | 4 | `raw_volume_pair` |
| +0x18 | 16 | `envelope` |
| +0x28 | 4 | `sample_position` (signed fixed-point behavior) |
| +0x2C | 2 | `block_address_units` |
| +0x2E | 1 | `manual_repeat_address` |
| +0x2F | 1 | `block_flags` |
| +0x30 | 2 | `pitch` |
| +0x32 | 2 | `repeat_address_units` |
| +0x34 | 6 | `history[3]`, signed 16-bit |
| +0x3A | 56 | `decoded[28]`, signed 16-bit |
| +0x72 | 1 | `stopped`, signed byte with distinct -1/0/+1 behavior |
| +0x73 | 1 | unresolved |

`VolumeSweep` (10 bytes): signed level +0, signed target +2, signed step +4,
countdown u16 +6, period u16 +8. Roles come from +0x10E4..+0x11C0 and
+0x1854/+0x18D4; implementing all sweep behavior is still pending.

`Envelope` (16 bytes): ADSR configuration u32 +0; phase i8 +4; exponential
flag u8 +5; threshold u16 +6; countdown u16 +8; signed step i16 +0xA;
period u16 +0xC; signed level i16 +0xE. The phase table at module +0xD4074
and consumers +0x1418..+0x17EC support this layout. Field identification is
broader than the currently implemented subset of phase transitions.

## 5. Structures that need unions or phase-specific views

The analysis record at `0x041B0000 + 4 * (guest_pc - base_pc)` is 16 bytes.
During analysis it contains flags, destination, opcode, category, cost, payload
and source fields (see `cpu_analysis_records.md`). During compilation:

- +0x04..+0x07 may become an emitted-code entry, replacing category/cost.
- +0x0C..+0x0F may become a patch-site address, replacing source-register bytes.

A single always-live C field layout would be misleading. Use phase-specific
views/union members and keep the original raw representation until consumers
are migrated together.

Likewise the list sentinel at GP+0x1B8 does NOT justify typing the following
16 bytes as an ordinary event: its apparent +8 deadline overlaps the separately
read control halfword at GP+0x1C0 and polling byte at GP+0x1C3. Preserve that
context/overlap until the empty-list lifecycle is fully recovered.

## 6. Ghidra type import and later application

`ghidra/ImportStateLayouts.java` imports the ten schema types plus a partial
16-KiB core view into a new analysis project. The successful export is
`out/ghidra-state.GMXnwg/export/repops_types.gdt`, with verified field/size
metadata in `types.json`. Instruction-record phase reuse is represented by
unions. Core, alternate-GP and ME base warnings are attached to selected code
entries; no universal GP register value is imposed.

These types are present in the new project's datatype manager, not applied over
program memory. In particular the script must never reinterpret module code at
0x10000 as core scratchpad. See `ghidra_state_types.md` for reproducible import
and the fresh project location. Existing Ghidra projects remain unchanged.

When migrating C later, replace one family of accesses at a time. Keep guest
addresses as u32 and use the existing little-endian accessors; casting directly
to native structs would mix host pointer width, alignment and aliasing assumptions.

No runtime refactor or mass offset replacement was performed in this pass.
Remaining priorities are alternate-GP lifecycle, frame/texture state boundaries,
CD/DMA/controller objects and ownership/alias lifetimes for scratch fields.
