#include "pops_emit.h"
#include <string.h>

static uint8_t byte(rp_context *c, uint32_t offset)
{
    return *(uint8_t *)rp_memory(c, c->gp + offset, 1);
}
static int32_t signed_byte(rp_context *c, uint32_t offset)
{
    const uint32_t b = byte(c, offset);
    return b < 128 ? (int32_t)b : (int32_t)b - 256;
}
static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void put_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static uint32_t emit(rp_context *c, uint32_t out, uint32_t instruction)
{
    rp_w32(c, out, instruction);
    return out + 4;
}
static uint32_t sign16(uint32_t value)
{
    return (value & 0xFFFF) | ((value & 0x8000) ? UINT32_C(0xFFFF0000) : 0);
}
static uint32_t saved_offset(uint32_t reg)
{
    return (reg & 0xE0) + ((reg & 7) << 2) + 0x180;
}
static bool known(rp_context *c, uint32_t reg)
{
    return (rp_u32(c, c->gp + 0xB58) & (UINT32_C(0x80000000) >> (reg & 31))) != 0;
}

/* Selected setup from +0x5D64..+0x5E44. Surrounding RAM prologue and cache
 * rollover paths are not supplied by this helper. Tables come from POPS.
 */
void rp_emit_init_registers(rp_context *c, uint32_t out)
{
    memset(rp_memory(c, c->gp + 0x740, 0x2C), 0, 0x2C);
    memcpy(rp_memory(c, c->gp + 0x76C, 12), rp_module_memory(c, 0xD40E8, 12), 12);
    memcpy(rp_memory(c, c->gp + 0x778, 32), rp_module_memory(c, 0xD40C8, 32), 32);
    rp_w32(c, c->gp + 0x740, out);
    rp_w32(c, c->gp + 0xB58, 0x80000000);
    rp_w32(c, c->gp + 0xB5C, 0);
}

/* +0x2950: nonnegative mappings are GPRs; values below -1 identify FPRs. */
int32_t rp_emit_lookup_register(rp_context *c, uint32_t reg)
{
    rp_function(c, 0x2950, "pops.lookup_register_location");
    int32_t mapped = signed_byte(c, 0x778 + reg);
    if (mapped < 0)
        for (unsigned i = 0; i < 12; ++i)
            if (byte(c, 0x760 + i) == reg) return byte(c, 0x76C + i);
    return mapped;
}

/* +0x2990 examines the preceding pair, not merely the immediately last word. */
uint32_t rp_emit_previous_movable(rp_context *c, uint32_t out)
{
    rp_function(c, 0x2990, "pops.previous_instruction_movable");
    if (out == rp_u32(c, c->gp + 0x740)) return 0;
    const uint32_t before_last = rp_u32(c, out - 8);
    if (before_last >> 29) return before_last >> 29;
    return ((before_last >> 26) & 7) ? 0 : (before_last & 0x38) - 8;
}

uint32_t rp_emit_temp(rp_context *c, uint32_t fallback, uint32_t dirty)
{
    rp_function(c, 0x2CB0, "pops.reserve_temporary_register");
    for (unsigned i = 0; i < 12; ++i) {
        if (byte(c, 0x760 + i) != 0) continue;
        const uint32_t host = byte(c, 0x76C + i);
        rp_w8(c, c->gp + 0x754 + i, (uint8_t)dirty);
        rp_w8(c, c->gp + 0x760 + i, 0x20);
        return host;
    }
    if (fallback == 2) rp_w32(c, c->gp + 0x744, 0);
    else rp_w8(c, c->gp + 0x750, 0);
    return fallback;
}

void rp_emit_release_temp(rp_context *c, uint32_t host)
{
    rp_function(c, 0x2CFC, "pops.release_temporary_register");
    if (host == 2 || host == 4) return;
    for (unsigned i = 0; i < 12; ++i) {
        if (byte(c, 0x76C + i) != host) continue;
        rp_w8(c, c->gp + 0x760 + i, 0); return;
    }
}

uint32_t rp_emit_debit(rp_context *c, int32_t cost, uint32_t out)
{
    rp_function(c, 0x44DC, "pops.emit_cycle_debit");
    return cost > 0 ? emit(c, out, 0x27390000 | ((0u - (uint32_t)cost) & 0xFFFF)) : out;
}

uint32_t rp_emit_spill_slot(rp_context *c, uint32_t slot, uint32_t out)
{
    rp_function(c, 0x2C34, "pops.spill_temporary_slot");
    if (!byte(c, 0x754 + slot)) return out;
    const uint32_t guest = byte(c, 0x760 + slot), host = byte(c, 0x76C + slot);
    rp_w8(c, c->gp + 0x754 + slot, 0);
    const int32_t mapped = guest < 32 ? signed_byte(c, 0x778 + guest) : -1;
    if (mapped == -1)
        return emit(c, out, 0xAF800000 | ((host & 31) << 16) | saved_offset(guest));
    return emit(c, out, 0x44800000 | ((host & 31) << 16) | (((0u - (uint32_t)mapped) & 31) << 11));
}

uint32_t rp_emit_load_register(rp_context *c, uint32_t out, uint32_t host, uint32_t guest)
{
    rp_function(c, 0x29C4, "pops.emit_register_reload");
    const int32_t mapped = signed_byte(c, 0x778 + guest);
    if (mapped == -1) return rp_emit_load_state(c, out, host | 0x80, saved_offset(guest));
    const uint32_t last = rp_u32(c, out - 4);
    if (rp_emit_previous_movable(c, out) &&
            (last & 0xFFE0FFFF) == 0x44800000 - (uint32_t)mapped * 0x800)
        return emit(c, out, (((last >> 16) & 31) << 21) | 0x21 | ((host & 31) << 11));
    return emit(c, out, 0x44000000 | ((host & 31) << 16) | (((0u - (uint32_t)mapped) & 31) << 11));
}

/* +0x2A88 exploits constants already held in registers, as well as the
 * generated-code GP=0x10000 and SP=0x09800000 conventions. */
uint32_t rp_emit_constant(rp_context *c, uint32_t out, uint32_t host, uint32_t value)
{
    rp_function(c, 0x2A88, "pops.emit_constant");
    const uint32_t dest = (host & 31) << 16, lo = value & 0xFFFF, hi = value >> 16;
    if (value == sign16(value)) return emit(c, out, 0x24000000 | dest | lo);
    if (!hi) return emit(c, out, 0x34000000 | dest | lo);
    if (!lo) return emit(c, out, 0x3C000000 | dest | hi);
    if (hi == 1) return emit(c, out, 0x37800000 | dest | lo);
    if (rp_u32(c, c->gp + 0x744) && rp_u32(c, c->gp + 0x74C) == hi)
        return emit(c, out, 0x34400000 | dest | lo);
    if (hi == 0x980) return emit(c, out, 0x37A00000 | dest | lo);
    for (uint32_t guest = 1; guest < 32; ++guest) {
        if (!known(c, guest)) continue;
        const uint32_t existing = rp_u32(c, c->gp + 0xB5C + guest * 4);
        const int32_t mapped = rp_emit_lookup_register(c, guest);
        const uint32_t delta = value - existing;
        if (mapped < 0) {
            if (value == existing) return rp_emit_load_register(c, out, host, guest);
        } else {
            if (delta == sign16(delta))
                return emit(c, out, 0x24000000 | (((uint32_t)mapped & 31) << 21) | dest | (delta & 0xFFFF));
            if (((value ^ existing) & 0xFFFF0000) == 0)
                return emit(c, out, 0x38000000 | (((uint32_t)mapped & 31) << 21) | dest | ((value ^ existing) & 0xFFFF));
        }
    }
    out = emit(c, out, 0x3C000000 | dest | hi);
    return emit(c, out, 0x34000000 | ((host & 31) << 21) | dest | lo);
}

/* +0x2E9C returns output_cursor<<3 | host_reg, exactly as POPS does. */
uint32_t rp_emit_allocate(rp_context *c, uint32_t out, uint32_t guest, uint32_t protected_regs, uint32_t mode)
{
    rp_function(c, 0x2E9C, "pops.allocate_guest_register");
    const uint32_t dirty = mode & 1;
    if (dirty && guest) {
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) & ~(UINT32_C(0x80000000) >> (guest & 31)));
        if (byte(c, 0x750) == guest) rp_w8(c, c->gp + 0x750, 0);
    }
    const int32_t mapped = signed_byte(c, 0x778 + guest);
    if (mapped >= 0) return (out << 3) | (uint32_t)mapped;
    uint32_t chosen = 0;
    for (uint32_t slot = 0; slot < 12; ++slot) {
        if (byte(c, 0x760 + slot) == 0) chosen = slot;
        if (byte(c, 0x760 + slot) == guest) {
            if (dirty) rp_w8(c, c->gp + 0x754 + slot, 1);
            return (out << 3) | byte(c, 0x76C + slot);
        }
    }
    /* Slot zero also acts as the original 'no candidate yet' sentinel. */
    if (chosen == 0) {
        uint32_t score = 100, slot = byte(c, 0x753), start = slot;
        if (start >= 12) rp_block(c, "allocator_cursor_out_of_domain", 0x2E9C);
        do {
            if (++slot > 11) slot = 0;
            const uint32_t victim = byte(c, 0x760 + slot);
            if (victim >= 32 || ((protected_regs >> (victim & 31)) & 1)) continue;
            uint32_t penalty = signed_byte(c, 0x778 + victim) == -1;
            if (byte(c, 0x754 + slot)) penalty = 2 * penalty + 1;
            penalty += !known(c, victim);
            if (penalty < score) { chosen = slot; score = penalty; }
        } while (slot != start);
        rp_w8(c, c->gp + 0x753, (uint8_t)chosen);
        out = rp_emit_spill_slot(c, chosen, out);
    }
    rp_w8(c, c->gp + 0x754 + chosen, (uint8_t)dirty);
    rp_w8(c, c->gp + 0x760 + chosen, (uint8_t)guest);
    const uint32_t host = byte(c, 0x76C + chosen);
    if (mode & 2) {
        const uint32_t value = rp_u32(c, c->gp + 0xB5C + guest * 4);
        if (known(c, guest) && value + UINT32_C(0x8000) < 0x28000)
            out = rp_emit_constant(c, out, host, value);
        else out = rp_emit_load_register(c, out, host, guest);
    }
    return (out << 3) | host;
}

uint32_t rp_emit_jump_delay(rp_context *c, uint32_t out, uint32_t target)
{
    rp_function(c, 0x308C, "pops.emit_jump_with_delay_slot");
    const uint32_t movable = rp_emit_previous_movable(c, out);
    rp_w32(c, c->gp + 0x744, 0); rp_w8(c, c->gp + 0x750, 0);
    uint32_t last = rp_u32(c, out - 4);
    if (!movable) { out += 4; last = 0; }
    rp_w32(c, out - 4, target >> 2);
    rp_w32(c, out, last);
    return out + 4;
}

uint32_t rp_emit_load_state(rp_context *c, uint32_t out, uint32_t destination, uint32_t offset)
{
    rp_function(c, 0x4504, "pops.emit_state_word_load");
    uint32_t host = destination & 0x7F;
    if (host == destination) {
        const uint32_t allocation = rp_emit_allocate(c, out, destination, 0, 1);
        out = (allocation >> 5) << 2; host = allocation & 31;
    }
    if (offset == 0x3C) offset = 0x38;
    if (offset == 0x70) offset = 0x74;
    if (offset == 0x74) {
        out = rp_emit_jump_delay(c, out, 0x300113A4);
        return emit(c, out, ((host & 31) << 11) | 0x00800021);
    }
    if (offset == 0xFC) {
        const uint32_t temp = rp_emit_temp(c, 4, 0);
        out = emit(c, out, 0x4860000F | ((host & 31) << 16));
        out = rp_emit_constant(c, out, temp, 0x7F87E000);
        out = emit(c, out, ((host & 31) << 21) | 0x24 | ((temp & 31) << 16) | ((temp & 31) << 11));
        out = emit(c, out, ((temp & 31) << 16) | 0x2B | ((temp & 31) << 11));
        out = emit(c, out, ((temp & 31) << 21) | 0x7C00FFC4 | ((host & 31) << 16));
        rp_emit_release_temp(c, temp); return out;
    }
    if (rp_emit_previous_movable(c, out)) {
        const uint32_t last = rp_u32(c, out - 4);
        if ((last & 0xFFE0FFFF) == offset + 0xAF800000)
            return emit(c, out, (((last >> 16) & 31) << 21) | 0x21 | ((host & 31) << 11));
        if ((last & 0xFFE0FFFF) == offset + 0xE7800000)
            return emit(c, out, (last & 0x1F0000) | 0x44000000 | ((host & 31) << 11));
    }
    return emit(c, out, 0x8F800000 | ((host & 31) << 16) | (offset & 0xFFFF));
}

uint32_t rp_emit_flush_hilo(rp_context *c, uint32_t out)
{
    rp_function(c, 0x2D3C, "pops.flush_HI_LO");
    if (!byte(c, 0x752)) return out;
    const uint32_t temp = rp_emit_temp(c, 2, 0);
    if (byte(c, 0x752) & 1) {
        out = emit(c, out, ((temp & 31) << 11) | 0x10);
        out = emit(c, out, ((temp & 31) << 16) | 0xAF8001A4);
    }
    if (byte(c, 0x752) & 2) {
        out = emit(c, out, ((temp & 31) << 11) | 0x12);
        out = emit(c, out, ((temp & 31) << 16) | 0xAF8001A8);
    }
    rp_w8(c, c->gp + 0x752, 0);
    rp_emit_release_temp(c, temp); return out;
}

uint32_t rp_emit_flush_registers(rp_context *c, uint32_t out, uint32_t last_slot)
{
    rp_function(c, 0x2DE8, "pops.flush_register_slots");
    if (last_slot > 11) rp_block(c, "register_slot_out_of_domain", 0x2DE8);
    for (int slot = (int)last_slot; slot >= 0; --slot) {
        if (!byte(c, 0x760 + (uint32_t)slot)) continue;
        out = rp_emit_spill_slot(c, (uint32_t)slot, out);
        rp_w8(c, c->gp + 0x760 + (uint32_t)slot, 0);
    }
    rp_w32(c, c->gp + 0x744, 0); rp_w8(c, c->gp + 0x750, 0);
    out = rp_emit_flush_hilo(c, out);
    rp_w8(c, c->gp + 0x751, 0);
    return out;
}

uint32_t rp_emit_pair(rp_context *c, uint32_t out, uint32_t dest, uint32_t src,
                      uint32_t *host_dest, uint32_t *host_src)
{
    rp_function(c, 0x30E0, "pops.allocate_register_pair");
    uint32_t allocation = rp_emit_allocate(c, out, dest, 1u << (src & 31), dest == src ? 3 : 1);
    uint32_t cursor = (allocation >> 5) << 2;
    *host_dest = allocation & 31;
    if (dest == src) {
        *host_src = *host_dest;
        if (rp_emit_previous_movable(c, out)) {
            const uint32_t last = rp_u32(c, out - 4);
            if ((last & 0xFC1FFFFF) == (*host_dest << 11) + 0x21) {
                cursor -= 4; *host_src = (last >> 21) & 31;
            }
        }
    } else {
        allocation = rp_emit_allocate(c, cursor, src, 1u << (dest & 31), 2);
        cursor = (allocation >> 5) << 2; *host_src = allocation & 31;
    }
    return cursor;
}

uint32_t rp_emit_immediate(rp_context *c, uint32_t op, uint32_t dest, uint32_t src,
                           uint32_t value, uint32_t out)
{
    rp_function(c, 0x31D0, "pops.emit_immediate_operation");
    uint32_t hd, hs;
    out = rp_emit_pair(c, out, dest, src, &hd, &hs);
    if (op == 0x80) {
        const int32_t mapped = signed_byte(c, 0x778 + dest);
        if (value != 0 || mapped >= -1) return rp_emit_constant(c, out, hd, value);
        for (unsigned slot = 0; slot < 12; ++slot) {
            if (byte(c, 0x760 + slot) != dest) continue;
            rp_w8(c, c->gp + 0x754 + slot, 0); rp_w8(c, c->gp + 0x760 + slot, 0); break;
        }
        return emit(c, out, 0x44800000 | (((0u - (uint32_t)mapped) & 31) << 11));
    }
    if (op == 0x81) return emit(c, out, 0x7C000420 | ((hs & 31) << 16) | ((hd & 31) << 11));
    if (op == 0x82 || (op == 0xC && (value == 0xFF || value == 0xFFFF))) {
        if (rp_emit_previous_movable(c, out)) {
            const uint32_t last = rp_u32(c, out - 4), kind = (last >> 16) & 0xFC1F;
            if (kind == hs + 0x8C00 || kind == hs + 0x9400 || kind == hs + 0x8400) {
                if (hd == hs) out -= 4;
                const uint32_t load = op == 0x82 ? 0x84000000 : value == 0xFF ? 0x90000000 : 0x94000000;
                return emit(c, out, load | (last & 0x03E00000) | ((hd & 31) << 16) | (last & 0xFFFF));
            }
        }
        if (op == 0x82) return emit(c, out, 0x7C000620 | ((hs & 31) << 16) | ((hd & 31) << 11));
    }
    return emit(c, out, (op << 26) | ((hs & 31) << 21) | ((hd & 31) << 16) | (value & 0xFFFF));
}

uint32_t rp_emit_known_value(rp_context *c, uint32_t dest, uint32_t value, uint32_t out, uint32_t record)
{
    rp_function(c, 0x4EC4, "pops.emit_and_track_known_value");
    if (known(c, dest) && rp_u32(c, c->gp + 0xB5C + dest * 4) == value) return out;
    bool deferred = false;
    if (record && (half(c, record) & 0x8000)) {
        const uint32_t next_dest = *(uint8_t *)rp_memory(c, record + 18, 1);
        const uint32_t next_op = *(uint8_t *)rp_memory(c, record + 19, 1);
        uint8_t *next_source = rp_memory(c, record + 28, 1);
        if (next_dest == dest && *next_source == dest && (next_op == 9 || next_op == 0xD)) {
            value = next_op == 9 ? value + sign16(half(c, record + 24)) : value | half(c, record + 24);
            put_half(c, record + 20, 0);
        } else if ((next_op & 0xF8) == 0x20 && (next_op & 3) != 2) {
            if (next_dest == dest && *next_source == dest) deferred = true;
            else if ((half(c, record + 16) & 0x8000) && *(uint8_t *)rp_memory(c, record + 34, 1) == dest) {
                const uint32_t after_op = *(uint8_t *)rp_memory(c, record + 35, 1);
                if (after_op == 0xF && *next_source == dest) {
                    *next_source = (uint8_t)next_dest; dest = next_dest; deferred = true;
                }
                if ((after_op & 0xF8) == 0x20 && (after_op & 3) != 2) deferred = true;
            }
        }
    }
    if (!deferred) out = rp_emit_immediate(c, 0x80, dest, 0, value, out);
    rp_w32(c, c->gp + 0xB5C + dest * 4, value);
    rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) | (UINT32_C(0x80000000) >> (dest & 31)));
    return out;
}

/* +0x6914, category 9 (immediates) and 0x13 (elided operation) only. Other
 * categories remain the next reconstruction task, not successful no-ops.
 */
uint32_t rp_emit_record(rp_context *c, uint32_t category, uint32_t record, uint32_t out, uint32_t cost)
{
    rp_function(c, 0x6914, "pops.emit_instruction_record_partial");
    (void)cost;
    if (category == 0x13) return out;
    if (category != 9) rp_block(c, "emitter_category_not_reconstructed", category);
    const uint32_t src = *(uint8_t *)rp_memory(c, record + 12, 1);
    const uint32_t dest = *(uint8_t *)rp_memory(c, record + 2, 1);
    const uint32_t op = *(uint8_t *)rp_memory(c, record + 3, 1);
    const uint32_t immediate = half(c, record + 8);
    if (!known(c, src)) return rp_emit_immediate(c, op, dest, src, immediate, out);
    uint32_t value = rp_u32(c, c->gp + 0xB5C + src * 4);
    switch (op) {
    case 0xA: value = (int32_t)value < (int32_t)sign16(immediate); break;
    case 0xB: value = value < sign16(immediate); break;
    case 0xC: value &= immediate; break;
    case 0xD: value |= immediate; break;
    case 0xE: value ^= immediate; break;
    case 0xF: value = immediate << 16; break;
    default: value += sign16(immediate); break;
    }
    return rp_emit_known_value(c, dest, value, out, record);
}
