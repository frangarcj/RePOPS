#include "pops_emit.h"
#include "pops_gte.h"
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
    return (rp_u32(c, RP_EMIT_ADDRESS(c, known_register_mask)) &
            (UINT32_C(0x80000000) >> (reg & 31))) != 0;
}

/* Selected setup from +0x5D64..+0x5E44. Surrounding RAM prologue and cache
 * rollover paths are not supplied by this helper. Tables come from POPS.
 */
void rp_emit_init_registers(rp_context *c, uint32_t out)
{
    memset(rp_memory(c, c->gp + 0x740, 0x2C), 0, 0x2C);
    memcpy(rp_memory(c, RP_EMIT_ADDRESS(c, temporary_host), 12), rp_module_memory(c, 0xD40E8, 12), 12);
    memcpy(rp_memory(c, RP_EMIT_ADDRESS(c, register_location), 32), rp_module_memory(c, 0xD40C8, 32), 32);
    rp_w32(c, RP_EMIT_ADDRESS(c, block_begin), out);
    rp_w32(c, RP_EMIT_ADDRESS(c, known_register_mask), 0x80000000);
    rp_w32(c, RP_EMIT_ADDRESS(c, known_register_values[0]), 0);
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
    if (op == RP_EMIT_CONSTANT) {
        const int32_t mapped = signed_byte(c, 0x778 + dest);
        if (value != 0 || mapped >= -1) return rp_emit_constant(c, out, hd, value);
        for (unsigned slot = 0; slot < 12; ++slot) {
            if (byte(c, 0x760 + slot) != dest) continue;
            rp_w8(c, c->gp + 0x754 + slot, 0); rp_w8(c, c->gp + 0x760 + slot, 0); break;
        }
        return emit(c, out, 0x44800000 | (((0u - (uint32_t)mapped) & 31) << 11));
    }
    if (op == RP_EMIT_SIGN_BYTE) return emit(c, out, 0x7C000420 | ((hs & 31) << 16) | ((hd & 31) << 11));
    if (op == RP_EMIT_SIGN_HALF || (op == RP_OP_ANDI && (value == 0xFF || value == 0xFFFF))) {
        if (rp_emit_previous_movable(c, out)) {
            const uint32_t last = rp_u32(c, out - 4), kind = (last >> 16) & 0xFC1F;
            if (kind == hs + 0x8C00 || kind == hs + 0x9400 || kind == hs + 0x8400) {
                if (hd == hs) out -= 4;
                const uint32_t load = op == RP_EMIT_SIGN_HALF ? 0x84000000 : value == 0xFF ? 0x90000000 : 0x94000000;
                return emit(c, out, load | (last & 0x03E00000) | ((hd & 31) << 16) | (last & 0xFFFF));
            }
        }
        if (op == RP_EMIT_SIGN_HALF) return emit(c, out, 0x7C000620 | ((hs & 31) << 16) | ((hd & 31) << 11));
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
        if (next_dest == dest && *next_source == dest && (next_op == RP_OP_ADDIU || next_op == RP_OP_ORI)) {
            value = next_op == RP_OP_ADDIU ? value + sign16(half(c, record + 24)) : value | half(c, record + 24);
            put_half(c, record + 20, 0);
        } else if ((next_op & 0xF8) == 0x20 && (next_op & 3) != 2) {
            if (next_dest == dest && *next_source == dest) deferred = true;
            else if ((half(c, record + 16) & 0x8000) && *(uint8_t *)rp_memory(c, record + 34, 1) == dest) {
                const uint32_t after_op = *(uint8_t *)rp_memory(c, record + 35, 1);
                if (after_op == RP_OP_LUI && *next_source == dest) {
                    *next_source = (uint8_t)next_dest; dest = next_dest; deferred = true;
                }
                if ((after_op & 0xF8) == 0x20 && (after_op & 3) != 2) deferred = true;
            }
        }
    }
    if (!deferred) out = rp_emit_immediate(c, RP_EMIT_CONSTANT, dest, 0, value, out);
    rp_w32(c, c->gp + 0xB5C + dest * 4, value);
    rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) | (UINT32_C(0x80000000) >> (dest & 31)));
    return out;
}

/* +0x42A4: leave the target field for the final linker and preserve the
 * previous instruction as a delay slot only when its cost is zero.
 */
static uint32_t emit_link_jump(rp_context *c, uint32_t out, uint32_t patch_slot,
                               int32_t cost, uint32_t target)
{
    rp_function(c, 0x42A4, "pops.emit_linkable_jump");
    out = rp_emit_spill_all(c, out);
    uint32_t delay;
    if (cost == 0 && rp_emit_previous_movable(c, out)) {
        out -= 4;
        delay = rp_u32(c, out);
    } else {
        if (cost < 0) cost = 0;
        delay = 0x27390000 | ((0u - (uint32_t)cost) & 0xFFFF);
    }
    rp_w32(c, patch_slot, out);
    out = emit(c, out, (target + UINT32_C(0x30000000)) >> 2);
    return emit(c, out, delay);
}

/* +0x6914 category 0x0E: a forward jump over empty records can become
 * fallthrough. Keep the cycle debit even when no branch word is needed.
 */
static uint32_t emit_forward_jump(rp_context *c, uint32_t record, uint32_t out, uint32_t cost)
{
    cost += rp_u32(c, c->gp + 0x6C4);
    if (!(half(c, record) & 4)) {
        out = rp_emit_debit(c, (int32_t)cost, out);
        out = rp_emit_exit_target(c, rp_u32(c, record + 8), out);
        rp_w32(c, c->gp + 0xB44, 0);
        return out;
    }
    const uint32_t target = 0x041B0000 +
        (rp_u32(c, record + 8) - rp_u32(c, c->gp + 0xB50)) * 4;
    if (target <= record) {
        out = rp_emit_debit(c, (int32_t)cost, out);
        out = rp_emit_spill_all(c, out);
        const uint32_t entry = rp_u32(c, target + 4);
        out = emit(c, out, 0x1F200000 | (((entry - out - 4) >> 2) & 0xFFFF));
        out = rp_emit_constant(c, out, 31, entry);
        out = rp_emit_constant(c, out, 2, rp_u32(c, record + 8));
        const uint32_t delay = rp_u32(c, out - 4);
        rp_w32(c, out - 4, 0x0800069A);
        out = emit(c, out, delay);
        rp_w32(c, c->gp + 0xB44, 0);
        return out;
    }
    uint32_t next = record + 32;
    while (next < target && half(c, next + 4) == 0) next += 16;
    if (next != target) {
        put_half(c, record, half(c, record) | 2);
        out = emit_link_jump(c, out, record + 12, (int32_t)cost, 0);
    } else {
        out = rp_emit_debit(c, (int32_t)cost, out);
    }
    rp_w32(c, c->gp + 0xB44, 0);
    return out;
}

/* +0x5064: preserve the pre-delay condition when the slot overwrites a source.
 * The temporary has the original synthetic guest tag 0x20. */
uint32_t rp_emit_capture_branch(rp_context *c, uint32_t record, uint32_t out)
{
    rp_function(c, 0x5064, "pops.capture_branch_condition_before_delay");
    const uint8_t *r = rp_memory(c, record, 16);
    const uint32_t left = r[12], right = r[13];
    const uint32_t temporary = rp_emit_temp(c, 4, 1);
    if (left && right) {
        const uint32_t a = rp_emit_allocate(c, out, left, 1u << right, 2);
        const uint32_t b = rp_emit_allocate(c, (a >> 5) << 2, right, 1u << left, 2);
        out = emit(c, (b >> 5) << 2,
                   ((a & 31) << 21) | ((b & 31) << 16) | ((temporary & 31) << 11) | 0x26);
    } else {
        out = rp_emit_argument(c, out, temporary, left | right);
    }
    if (temporary == 4) out = emit(c, out, 0xAF8401A0);
    return out;
}

/* +0x4340. Store the patch address in the record before adding a debit in
 * the branch's delay slot. A negative source consumes a captured condition. */
uint32_t rp_emit_conditional_branch(rp_context *c, uint32_t opcode, uint32_t left,
                                  uint32_t right, uint32_t cost, uint32_t patch_slot, uint32_t out)
{
    rp_function(c, 0x4340, "pops.emit_conditional_branch");
    uint32_t a = 4, b = 0;
    if ((int32_t)left < 0) {
        bool found = false;
        for (unsigned i = 0; i < 12; ++i) {
            if (byte(c, 0x760 + i) <= 31) continue;
            a = byte(c, 0x76C + i);
            rp_w8(c, c->gp + 0x754 + i, 0);
            found = true;
            break;
        }
        if (!found) {
            rp_w8(c, c->gp + 0x750, 0);
            out = emit(c, out, 0x8F8401A0);
        }
    } else {
        const uint32_t x = rp_emit_allocate(c, out, left, 1u << (right & 31), 2);
        const uint32_t y = rp_emit_allocate(c, (x >> 5) << 2, right, 1u << (left & 31), 2);
        out = (y >> 5) << 2; a = x & 31; b = y & 31;
    }
    out = rp_emit_spill_all(c, out);
    rp_w32(c, patch_slot, out);
    const uint32_t branch = opcode >= RP_OP_BEQ && opcode <= RP_OP_BGTZ ?
        ((opcode - 0xBE) << 26) | (a << 21) | (b << 16) :
        0x04000000 | (a << 21) | (((opcode ^ 1) & 1) << 16);
    out = emit(c, out, branch);
    if ((int32_t)left < 0) rp_emit_release_temp(c, a);
    return emit(c, out, 0x27390000 | ((0u - cost) & 0xFFFF));
}

/* +0x6914 category 0x0C. Backward edges retain the T9 event check and the
 * +0x1A68 handoff, rather than becoming an unbounded host loop. */
static uint32_t emit_branch_record(rp_context *c, uint32_t record, uint32_t out, uint32_t cost)
{
    const uint8_t *r = rp_memory(c, record, 16);
    const uint32_t flags = half(c, record), opcode = r[3], right = r[13];
    const uint32_t left = flags & 0x20 ? UINT32_MAX : r[12];
    const uint32_t target_pc = rp_u32(c, record + 8);
    const uint32_t target_record = 0x041B0000 + (target_pc - rp_u32(c, c->gp + 0xB50)) * 4;
    if ((flags & 4) && target_record > record) {
        put_half(c, record, (uint16_t)(flags | 2));
        out = rp_emit_conditional_branch(c, opcode, left, right, cost, record + 12, out);
    } else {
        out = rp_emit_conditional_branch(c, opcode ^ 1, left, right, cost, record + 12, out);
        if (flags & 4) {
            const uint32_t target = rp_u32(c, target_record + 4);
            out = emit(c, out, 0x1F200000 | (((target - out - 4) >> 2) & 0xFFFF));
            out = rp_emit_constant(c, out, 31, target);
            out = rp_emit_constant(c, out, 2, target_pc);
            const uint32_t last = rp_u32(c, out - 4);
            rp_w32(c, out - 4, 0x0800069A);
            out = emit(c, out, last);
        } else {
            out = rp_emit_exit_target(c, target_pc, out);
        }
        const uint32_t patch = rp_u32(c, record + 12), word = rp_u32(c, patch);
        rp_w32(c, patch, (word & 0xFFFF0000) | (((out - patch - 4) >> 2) & 0xFFFF));
    }
    rp_w32(c, c->gp + 0xB44, 0);
    return out;
}

/* +0x6914 category 0x0D: retain the original constant propagation and register
 * allocation, including integer values cached as FPR bits. */
static uint32_t emit_known_alu(rp_context *c, uint32_t record, uint32_t out)
{
    const uint8_t *r = rp_memory(c, record, 16);
    const uint32_t dest = r[2], op = r[3], left = r[12], right = r[13];
    uint32_t value = 0;
    bool folded = false;
    if (known(c, left) && known(c, right)) {
        const uint32_t a = rp_u32(c, c->gp + 0xB5C + left * 4);
        const uint32_t b = rp_u32(c, c->gp + 0xB5C + right * 4);
        folded = true;
        switch (op) {
        case RP_OP_ADDU: value = a + b; break;
        case RP_OP_SUBU: value = a - b; break;
        case RP_OP_AND: value = a & b; break;
        case RP_OP_OR: value = a | b; break;
        default: folded = false; break;
        }
        if (folded && (a == 0 || b == 0))
            return rp_emit_known_value(c, dest, value, out, record);
    }
    const uint32_t function = op & ~0x40u;
    const int32_t destination = rp_emit_lookup_register(c, dest);
    if (destination < -1 && (function == 0x21 || function == 0x25) && (!left || !right)) {
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) & ~(0x80000000u >> (dest & 31)));
        if (byte(c, 0x750) == dest) rp_w8(c, c->gp + 0x750, 0);
        const uint32_t source = left | right;
        const int32_t source_location = rp_emit_lookup_register(c, source);
        const uint32_t fd = (0u - (uint32_t)destination) & 31;
        uint32_t word;
        if (source_location < -1)
            word = 0x46000006 | (fd << 6) | (((0u - (uint32_t)source_location) & 31) << 11);
        else if (source_location == -1)
            word = 0xC7800000 | (fd << 16) | ((source & 0xE0) + ((source & 7) << 2) + 0x180);
        else word = 0x44800000 | (((uint32_t)source_location & 31) << 16) | (fd << 11);
        out = emit(c, out, word);
    } else {
        uint32_t d, a, b;
        const uint32_t original_out = out;
        if (left == right) {
            out = rp_emit_pair(c, out, dest, left, &d, &a);
            b = a;
        } else {
            uint32_t allocation = rp_emit_allocate(c, out, dest,
                (1u << (left & 31)) | (1u << (right & 31)),
                (dest == left || dest == right) ? 3 : 1);
            d = allocation & 31; out = (allocation >> 5) << 2;
            if (dest == left) {
                a = d;
                const uint32_t previous = rp_u32(c, original_out - 4);
                if (rp_emit_previous_movable(c, original_out) &&
                        (previous & 0xFC1FFFFF) == ((d << 11) | 0x21)) {
                    a = (previous >> 21) & 31;
                    out -= 4;
                }
            } else {
                allocation = rp_emit_allocate(c, out, left,
                    (1u << (dest & 31)) | (1u << (right & 31)), 2);
                a = allocation & 31; out = (allocation >> 5) << 2;
            }
            if (dest == right) {
                b = d;
                const uint32_t previous = rp_u32(c, original_out - 4);
                if (rp_emit_previous_movable(c, original_out) &&
                        (previous & 0xFC1FFFFF) == ((d << 11) | 0x21)) {
                    b = (previous >> 21) & 31;
                    if (original_out != out) rp_w32(c, original_out - 4, rp_u32(c, original_out));
                    out -= 4;
                }
            } else {
                allocation = rp_emit_allocate(c, out, right,
                    (1u << (dest & 31)) | (1u << (left & 31)), 2);
                b = allocation & 31; out = (allocation >> 5) << 2;
            }
        }
        out = emit(c, out, function | (a << 21) | (b << 16) | (d << 11));
    }
    if (folded) {
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) | (0x80000000u >> (dest & 31)));
        rp_w32(c, c->gp + 0xB5C + dest * 4, value);
    }
    return out;
}

/* +0x6768. Keep the original target-table lookup and slow-entry links;
 * these are numeric Allegrex addresses, never native C function pointers.
 */
uint32_t rp_emit_exit_target(rp_context *c, uint32_t target, uint32_t out)
{
    rp_function(c, 0x6768, "pops.emit_exit_target");
    const uint32_t physical = target & 0x1FFFFFFF;
    uint32_t table = 0x09C00000, offset = target, linked = 0;
    bool fallback = false;
    if (physical >> 23) {
        table = 0x09E00000; offset = physical;
        fallback = physical + UINT32_C(0xE0400000) > 0x7FFFF;
    }
    if (!fallback) {
        linked = rp_u32(c, table + (offset & 0x1FFFFC));
        fallback = (int32_t)linked < 0;
    }
    if (fallback) {
        target = 0x600;
        linked = rp_u32(c, 0x09C00600);
    }
    out = rp_emit_spill_all(c, out);
    out = rp_emit_constant(c, out, 4, target);
    const uint32_t last = out - 4, delay = rp_u32(c, last);
    const uint32_t primary = rp_u32(c, c->gp + 0x3CF4);
    const uint32_t secondary = rp_u32(c, c->gp + 0x3CF8);
    bool far = false;
    uint32_t branch;
    if (last - primary < 0x20000)
        branch = 0x1B200000 | (((primary - last) / 4 - 1) & 0xFFFF);
    else if (last - secondary < 0x20000)
        branch = 0x1B200000 | (((secondary - last) / 4 - 1) & 0xFFFF);
    else { branch = 0x1B200003; far = true; }
    rp_w32(c, last, branch);
    if (!linked) linked = 0x2888;
    out = emit(c, out, delay);
    out = emit(c, out, (linked + UINT32_C(0x30000000)) >> 2);
    out = emit(c, out, 0xAF8401A0);
    if (far) {
        if (out > 0x09B7FFFF) rp_w32(c, c->gp + 0x3CF8, out);
        else rp_w32(c, c->gp + 0x3CF4, out);
        out = emit(c, out, 0x0C0006A0);
        out = emit(c, out, 0xAF8401A0);
    }
    return out;
}

/* +0x6FF0..+0x70EC follows the analyzed control flow until a flag read or
 * overwrite. Do not always choose the no-flags helper just because the
 * immediately following instruction does not read FLAG. */
static bool gte_flags_are_needed(rp_context *c, uint32_t record)
{
    const uint32_t stride = sizeof(rp_gte_record_layout);
    const uint32_t end = rp_u32(c, RP_EMIT_ADDRESS(c, last_analysis_record));
    uint32_t cursor = record + stride, furthest = record;
    for (unsigned steps = 0; cursor <= end; ++steps) {
        if (steps == 4096) rp_block(c, "GTE_flag_analysis_diagnostic_budget", cursor);
        const int8_t destination = *(int8_t *)rp_memory(c, RP_GTE_RECORD_ADDRESS(cursor, destination), 1);
        const uint8_t selector = *(uint8_t *)rp_memory(c, RP_COP_RECORD_ADDRESS(cursor, cop_register), 1);
        if (destination > 0 && selector == 63) return true;
        const uint16_t flags = half(c, RP_GTE_RECORD_ADDRESS(cursor, flags));
        if (flags & RP_RECORD_EXIT) return true;
        if (flags & RP_RECORD_DELAY_SLOT) {
            const uint32_t previous = cursor - stride;
            if (!(half(c, RP_GTE_RECORD_ADDRESS(previous, flags)) & RP_RECORD_LOCAL_TARGET)) return true;
            const uint32_t target = RP_ANALYSIS_RECORD_BASE +
                (rp_u32(c, RP_GTE_RECORD_ADDRESS(previous, command)) -
                 rp_u32(c, RP_EMIT_ADDRESS(c, analysis_base_pc))) * 4;
            const uint16_t target_flags = half(c, RP_GTE_RECORD_ADDRESS(target, flags));
            const uint8_t previous_op = *(uint8_t *)rp_memory(c, RP_GTE_RECORD_ADDRESS(previous, opcode), 1);
            if (target < record) {
                if (!(target_flags & RP_RECORD_GTE_FLAGS_OVERWRITTEN)) return true;
                if (previous_op < RP_OP_BGEZ) return false;
            } else if (previous_op >= RP_OP_BGEZ) {
                if (!(target_flags & RP_RECORD_GTE_FLAGS_OVERWRITTEN) && furthest < target)
                    furthest = target;
            } else {
                cursor = target;
                continue;
            }
        }
        if (cursor >= furthest && (flags & RP_RECORD_GTE_FLAGS_OVERWRITTEN)) return false;
        cursor += stride;
    }
    return true;
}

/* +0x6914, selected original categories. Unsupported paths remain explicit
 * boundaries rather than silently emitting a different execution strategy.
 */
uint32_t rp_emit_record(rp_context *c, rp_pops_category category, uint32_t record, uint32_t out, uint32_t cost)
{
    rp_function(c, 0x6914, "pops.emit_instruction_record_partial");
    cost += rp_u32(c, c->gp + 0xB44);
    if ((int32_t)cost < 2) cost = 2;
    if (category == RP_CAT_ELIDED) return out;
    if (category == RP_CAT_SHIFT_IMMEDIATE) {
        const uint8_t *r = rp_memory(c, record, 32);
        const uint32_t source = r[13], dest = r[2], op = r[3];
        const uint32_t amount = (rp_u32(c, record + 8) >> 6) & 31;
        const bool was_known = known(c, source);
        uint32_t value = rp_u32(c, c->gp + 0xB5C + source * 4);
        if ((half(c, record) & 0x8000) && half(c, record + 20) == RP_CAT_SHIFT_IMMEDIATE &&
                r[29] == r[18] && dest == r[29]) {
            const uint32_t next_amount = (rp_u32(c, record + 24) >> 6) & 31;
            if (op == RP_OP_SLL && r[19] == RP_OP_SRA &&
                next_amount == amount && (amount == 16 || amount == 24)) {
                out = rp_emit_immediate(c,
                        amount == 24 ? RP_EMIT_SIGN_BYTE : RP_EMIT_SIGN_HALF,
                        dest, source, 0, out);
                put_half(c, record + 20, RP_CAT_EMPTY);
                if (was_known) {
                    value = amount == 24 ?
                        (uint32_t)(int32_t)(int8_t)value :
                        (uint32_t)(int32_t)(int16_t)value;
                    rp_w32(c, c->gp + 0xB5C + dest * 4, value);
                    rp_w32(c, c->gp + 0xB58,
                           rp_u32(c, c->gp + 0xB58) |
                           (0x80000000u >> (dest & 31)));
                }
                return out;
            }
            if (dest == source && amount == next_amount &&
                (op == RP_OP_SRL || op == RP_OP_SRA) && r[19] == RP_OP_SLL) {
                const uint32_t allocation = rp_emit_allocate(c, out, dest, 0, 3);
                out = (allocation >> 5) << 2;
                const uint32_t host = allocation & 31;
                out = emit(c, out, 0x7C000004 | (host << 16) |
                           (((amount - 1) & 31) << 11));
                put_half(c, record + 20, RP_CAT_EMPTY);
                if (was_known) {
                    value &= UINT32_MAX << amount;
                    rp_w32(c, c->gp + 0xB5C + dest * 4, value);
                    rp_w32(c, c->gp + 0xB58,
                           rp_u32(c, c->gp + 0xB58) |
                           (0x80000000u >> (dest & 31)));
                }
                return out;
            }
            if (op == RP_OP_SLL && r[19] == RP_OP_SRL && next_amount >= amount) {
                rp_event(c, "compiler_boundary", "shift_pair_words",
                         rp_u32(c, record + 8), rp_u32(c, record + 24));
                rp_event(c, "compiler_boundary", "shift_pair_regs",
                         (source << 24) | (dest << 16) | (r[29] << 8) | r[18],
                         (op << 8) | r[19]);
                rp_block(c, "shift_pair_peephole_not_reconstructed", 0x6914);
            }
        }
        uint32_t host_dest, host_source;
        out = rp_emit_pair(c, out, dest, source, &host_dest, &host_source);
        out = emit(c, out, (op & ~0x40u) | (amount << 6) |
                   ((host_source & 31) << 16) | ((host_dest & 31) << 11));
        if (was_known) {
            if (op == RP_OP_SLL) value <<= amount;
            else if (op == RP_OP_SRL) value >>= amount;
            else value = (uint32_t)((int32_t)value >> amount);
            rp_w32(c, c->gp + 0xB5C + dest * 4, value);
            rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) | (0x80000000u >> (dest & 31)));
        }
        return out;
    }
    if (category == RP_CAT_MULT_DIV) {
        rp_w8(c, c->gp + 0x751, 3);
        rp_w8(c, c->gp + 0x752, 3);
        return emit_known_alu(c, record, out);
    }
    if (category == RP_CAT_READ_HILO || category == RP_CAT_WRITE_HILO) {
        const uint8_t *r = rp_memory(c, record, 16);
        const uint32_t op = r[3];
        const uint32_t lo = (op & 2) != 0;
        const uint32_t bit = lo ? 2 : 1;
        const uint32_t offset = lo ? 0x1A8 : 0x1A4;
        const uint32_t guest = category == RP_CAT_READ_HILO ? r[2] : r[12];
        if (category == RP_CAT_WRITE_HILO) {
            rp_w8(c, c->gp + 0x751, byte(c, 0x751) & (uint8_t)~bit);
            rp_w8(c, c->gp + 0x752, byte(c, 0x752) & (uint8_t)~bit);
            return rp_emit_store_state(c, 0, guest, offset, out);
        }
        if (!(byte(c, 0x751) & bit))
            return rp_emit_load_state(c, out, guest, offset);
        uint32_t allocation = rp_emit_allocate(c, out, guest, 0, 1);
        out = (allocation >> 5) << 2;
        const uint32_t host = allocation & 31;
        out = emit(c, out, (host << 11) | (lo ? 0x12 : 0x10));
        if (byte(c, 0x752) & bit) {
            rp_w8(c, c->gp + 0x752, byte(c, 0x752) & (uint8_t)~bit);
            out = emit(c, out, 0xAF800000 | (host << 16) | offset);
        }
        return out;
    }
    if (category == RP_CAT_EXCEPTION) {
        const uint8_t *r = rp_memory(c, record, 16);
        uint32_t cause;
        switch (r[3]) {
        case 0x11: case 0x31: case 0x39: cause = 0x0400000B; break;
        case 0x13: case 0x33: case 0x3B: cause = 0x0C00000B; break;
        case RP_OP_SYSCALL: cause = 8; break;
        case RP_OP_BREAK: cause = 9; break;
        default: cause = 10; break;
        }
        uint32_t pc = rp_u32(c, c->gp + 0xB50) +
                      ((record - UINT32_C(0x041B0000)) >> 2);
        if (half(c, record) & 1) {
            cause |= 0x20000000;
            pc -= 4;
        }
        const uint32_t temp = rp_emit_temp(c, 4, 0);
        out = rp_emit_debit(c, (int32_t)cost, out);
        out = rp_emit_constant(c, out, temp, pc);
        out = emit(c, out, 0xAF800138 | ((temp & 31) << 16));
        rp_emit_release_temp(c, temp);
        out = rp_emit_flush_registers(c, out, 11);
        out = emit(c, out, 0x24040000 | (cause & 0xFFFF));
        if (cause & 0xFFFF0000) {
            out = rp_emit_constant(c, out, 5, (uint32_t)((int32_t)cause >> 22));
            out = emit(c, out, 0xA3850137);
        }
        out = emit(c, out, 0x0C002531);
        out = emit(c, out, 0xAF9901B0);
        out = emit(c, out, 0x00400008);
        out = emit(c, out, 0x8F9901B0);
        rp_w32(c, c->gp + 0xB44, 0);
        return out;
    }
    if (category == RP_CAT_READ_COP) {
        const uint8_t *r = rp_memory(c, record, 16);
        out = rp_emit_load_state(c, out, r[2], (uint32_t)r[14] << 2);
        if (r[14] == 0x3A)
            out = rp_emit_immediate(c, RP_EMIT_SIGN_HALF, r[2], r[2], 0, out);
        return out;
    }
    if (category == RP_CAT_LOAD_COP_MEMORY || category == RP_CAT_STORE_COP_MEMORY) {
        const uint32_t cop = *(uint8_t *)rp_memory(c, RP_COP_RECORD_ADDRESS(record, cop_register), 1);
        const uint32_t base = (uint32_t)(int32_t)*(int8_t *)rp_memory(c,
                                      RP_COP_RECORD_ADDRESS(record, base_register), 1);
        const uint32_t displacement = sign16(half(c, RP_COP_RECORD_ADDRESS(record, displacement)));
        const uint32_t state_offset = cop * sizeof(uint32_t);
        if (category == RP_CAT_STORE_COP_MEMORY) {
            out = rp_emit_load_state(c, out, RP_EMIT_HOST_A1, state_offset);
            return rp_emit_memory(c, RP_OP_SW, RP_EMIT_HOST_A1, base, displacement, out, 0);
        }
        const uint8_t policy = *(uint8_t *)rp_module_memory(c, RP_COP_WRITE_POLICY_TABLE + cop, 1);
        const rp_pops_opcode op = policy == RP_STATE_STORE_SIGNED_HALF_WORD ? RP_OP_LH :
                                 policy == RP_STATE_STORE_HALF ? RP_OP_LHU : RP_OP_LW;
        /* Even an ignored COP destination performs the original memory read. */
        out = rp_emit_memory(c, op, RP_EMIT_HOST_V0, base, displacement, out, 0);
        return rp_emit_store_state(c, policy, RP_EMIT_HOST_V0, state_offset, out);
    }
    if (category == RP_CAT_COP0_CONTROL) {
        out = emit(c, out, 0x8F850130);
        out = emit(c, out, 0x00053082);
        out = emit(c, out, 0x7CC51804);
        out = emit(c, out, 0xAF850130);
        out = rp_emit_flush_registers(c, out, 11);
        out = emit(c, out, 0x0C0025AB);
        out = emit(c, out, 0xAF9901B0);
        return emit(c, out, 0x8F9901B0);
    }
    if (category == RP_CAT_MEMORY) return rp_emit_memory_record(c, record, out);
    if (category == RP_CAT_JUMP_REGISTER) {
        out = emit_link_jump(c, out, record + 12, (int32_t)cost, 0x2648);
        rp_w32(c, c->gp + 0xB44, 0);
        return out;
    }
    if (category == RP_CAT_JUMP_DIRECT) return emit_forward_jump(c, record, out, cost);
    if (category == RP_CAT_BRANCH) return emit_branch_record(c, record, out, cost);
    if (category == RP_CAT_ALU) return emit_known_alu(c, record, out);
    if (category == RP_CAT_WRITE_COP) {
        const uint8_t *r = rp_memory(c, record, 16);
        if (r[14] == 0x43 && !known(c, r[13]) && record != 0x041B0000) {
            const uint8_t previous = *(uint8_t *)rp_memory(c, record - 13, 1);
            const uint32_t configured = rp_u32(c, 0x09E812B0);
            if ((uint8_t)(previous + 0x9C) < 2 && configured && configured != 0x72D0EE59) {
                const uint32_t temporary = rp_emit_temp(c, 4, 0);
                out = rp_emit_constant(c, out, temporary, configured ^ UINT32_C(0x72D0EE59));
                out = emit(c, out, 0xAF80010C | ((temporary & 31) << 16));
                rp_emit_release_temp(c, temporary);
                return out;
            }
        }
        const uint8_t policy = *(uint8_t *)rp_module_memory(c, RP_COP_WRITE_POLICY_TABLE + r[14], 1);
        return rp_emit_store_state(c, policy, r[13], (uint32_t)r[14] * 4, out);
    }
    if (category == RP_CAT_EXIT) {
        out = rp_emit_debit(c, (int32_t)cost, out);
        out = rp_emit_exit_target(c, rp_u32(c, c->gp + 0xB50) + ((record - 0x041B0000) >> 2), out);
        rp_w32(c, c->gp + 0xB44, 0);
        return out;
    }
    if (category == RP_CAT_GTE) {
        const uint32_t command = rp_u32(c, RP_GTE_RECORD_ADDRESS(record, command));
        rp_event(c, "GTE_compile", "original_command", record, command);
        if ((command & 63) == RP_GTE_NCDS) {
            /* The VFPU helper leaves the allocator's GPR slots and HI/LO live. */
            rp_event(c, "GTE_compile", "selected_NCDS_helper", record, RP_GTE_NCDS_HELPER);
            return rp_emit_jump_delay(c, out, UINT32_C(0x30000000) + RP_GTE_NCDS_HELPER);
        }
        if ((command & 63) == RP_GTE_AVSZ3 || (command & 63) == RP_GTE_AVSZ4) {
            const uint32_t helper = (command & 63) == RP_GTE_AVSZ3 ?
                RP_GTE_AVSZ3_HELPER : RP_GTE_AVSZ4_HELPER;
            /* +0x7154/+0x716C flush HI/LO only, leaving GPR slots alive. */
            out = rp_emit_flush_hilo(c, out);
            rp_w8(c, RP_EMIT_ADDRESS(c, hilo_cached), 0);
            rp_event(c, "GTE_compile", "selected_AVSZ_helper", record, helper);
            return rp_emit_jump_delay(c, out, UINT32_C(0x30000000) + helper);
        }
        if ((command & 63) == RP_GTE_NCLIP) {
            /* +0x71E8 selects last_slot=0: higher temporaries remain live. */
            out = rp_emit_flush_registers(c, out, 0);
            rp_event(c, "GTE_compile", "selected_NCLIP_helper", record, RP_GTE_NCLIP_HELPER);
            return rp_emit_jump_delay(c, out, UINT32_C(0x30000000) + RP_GTE_NCLIP_HELPER);
        }
        if ((command & 63) != RP_GTE_RTPT)
            rp_block(c, "GTE_command_emitter_not_reconstructed", command & 63);
        const uint32_t helper = gte_flags_are_needed(c, record) ?
            RP_GTE_RTPT_FLAGS_HELPER : RP_GTE_RTPT_NO_FLAGS_HELPER;
        out = rp_emit_flush_registers(c, out, 11);
        rp_event(c, "GTE_compile", "selected_RTPT_helper", record, helper);
        return rp_emit_jump_delay(c, out, UINT32_C(0x30000000) + helper);
    }
    if (category != RP_CAT_IMMEDIATE) rp_block(c, "emitter_category_not_reconstructed", category);
    const uint32_t src = *(uint8_t *)rp_memory(c, record + 12, 1);
    const uint32_t dest = *(uint8_t *)rp_memory(c, record + 2, 1);
    const uint32_t op = *(uint8_t *)rp_memory(c, record + 3, 1);
    const uint32_t immediate = half(c, record + 8);
    if (!known(c, src)) return rp_emit_immediate(c, op, dest, src, immediate, out);
    uint32_t value = rp_u32(c, c->gp + 0xB5C + src * 4);
    switch (op) {
    case RP_OP_SLTI: value = (int32_t)value < (int32_t)sign16(immediate); break;
    case RP_OP_SLTIU: value = value < sign16(immediate); break;
    case RP_OP_ANDI: value &= immediate; break;
    case RP_OP_ORI: value |= immediate; break;
    case RP_OP_XORI: value ^= immediate; break;
    case RP_OP_LUI: value = immediate << 16; break;
    default: value += sign16(immediate); break;
    }
    return rp_emit_known_value(c, dest, value, out, record);
}
