#include "pops_emit.h"

static uint8_t byte(rp_context *c, uint32_t offset)
{
    return *(uint8_t *)rp_memory(c, c->gp + offset, 1);
}
static int32_t location(rp_context *c, uint32_t reg)
{
    uint32_t v = byte(c, 0x778 + reg);
    return v < 128 ? (int32_t)v : (int32_t)v - 256;
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
    rp_w32(c, out, instruction); return out + 4;
}
static uint32_t sign16(uint32_t value)
{
    return (value & 0xFFFF) | ((value & 0x8000) ? UINT32_C(0xFFFF0000) : 0);
}
static bool known(rp_context *c, uint32_t reg)
{
    return (rp_u32(c, c->gp + 0xB58) & (UINT32_C(0x80000000) >> (reg & 31))) != 0;
}
static void invalidate_temporary_names(rp_context *c)
{
    for (unsigned i = 0; i < 12; ++i) rp_w8(c, c->gp + 0x760 + i, 0);
    rp_w32(c, c->gp + 0x744, 0);
    rp_w8(c, c->gp + 0x750, 0); rp_w8(c, c->gp + 0x751, 0);
}

/* +0x2E58 spills values but retains names until the caller invalidates them. */
uint32_t rp_emit_spill_all(rp_context *c, uint32_t out)
{
    rp_function(c, 0x2E58, "pops.spill_all_temporaries");
    for (int i = 11; i >= 0; --i) out = rp_emit_spill_slot(c, (uint32_t)i, out);
    return rp_emit_flush_hilo(c, out);
}

uint32_t rp_emit_argument(rp_context *c, uint32_t out, uint32_t host, uint32_t guest)
{
    rp_function(c, 0x3458, "pops.emit_argument_register");
    if (host == 4) rp_w8(c, c->gp + 0x750, 0);
    if (host == 2) rp_w32(c, c->gp + 0x744, 0);
    if ((guest & 0x7F) == guest) {
        const int32_t mapped = rp_emit_lookup_register(c, guest);
        if (mapped < 0) return rp_emit_load_register(c, out, host, guest);
        if (rp_emit_previous_movable(c, out) &&
                rp_u32(c, out - 4) == (uint32_t)mapped * 0x800 + host * 0x200000 + 0x21)
            return out;
        return emit(c, out, (((uint32_t)mapped & 31) << 21) | ((host & 31) << 11) | 0x21);
    }
    return (guest & 0x7F) == host ? out :
           emit(c, out, ((guest & 31) << 21) | ((host & 31) << 11) | 0x21);
}

uint32_t rp_emit_result(rp_context *c, uint32_t out, uint32_t guest, uint32_t direct)
{
    rp_function(c, 0x3544, "pops.emit_result_register");
    uint32_t host = guest & 0x7F;
    if (host == guest) {
        if (!guest) return out;
        const int32_t mapped = location(c, guest);
        if (mapped < 0 && direct) {
            for (unsigned i = 0; i < 12; ++i) {
                if (byte(c, 0x760 + i) != guest) continue;
                rp_w8(c, c->gp + 0x754 + i, 0); rp_w8(c, c->gp + 0x760 + i, 0); break;
            }
            if (mapped == -1)
                return emit(c, out, 0xAF820000 | ((guest & 0xE0) + ((guest & 7) << 2) + 0x180));
            return emit(c, out, 0x44820000 | (((0u - (uint32_t)mapped) & 31) << 11));
        }
        const uint32_t result = rp_emit_allocate(c, out, guest, 0, 1);
        host = result & 31; out = (result >> 5) << 2;
    } else if (host == 2) return out;
    return emit(c, out, 0x00400021 | ((host & 31) << 11));
}

/* +0x88BC's side-effect-free constant-read cases are used by the compiler.
 * This is an actual POPS helper, not a full host PS1 bus implementation.
 */
uint32_t rp_pops_constant_read(rp_context *c, uint32_t address, uint32_t width)
{
    rp_function(c, 0x88BC, "pops.read_constant_memory");
    if (address == 0xFFFE0130) return rp_u32(c, c->gp + 0x1E0);
    if (address != 0x1F802030 && address != 0x1F802040) {
        if (address + UINT32_C(0xE1000000) < 0x800000) return 0;
        const uint32_t physical = address & 0x1FFFFFFF;
        if (physical + UINT32_C(0xE0400000) < 0x80000) {
            const uint32_t offset = physical + UINT32_C(0xE0453C20);
            const uint8_t *p = rp_module_memory(c, offset, width == 2 ? 4 : 2);
            switch (width) {
            case 0: return (uint32_t)(p[0] < 128 ? p[0] : (int32_t)p[0] - 256);
            case 1: return sign16(p[0] | (uint32_t)p[1] << 8);
            case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            case 4: return p[0];
            default: return p[0] | (uint32_t)p[1] << 8;
            }
        }
    }
    if ((int32_t)(width - 4) > 0) return 0xFFFF;
    return width == 4 ? 0xFF : UINT32_MAX;
}

/* +0x362C specializes an address whose class is already known. Class 0
 * resolves the existing guest I/O table. Its values stay numeric code offsets.
 */
uint32_t rp_emit_fixed_memory(rp_context *c, uint32_t out, uint32_t op, uint32_t guest,
                              uint32_t kind, uint32_t address, uint32_t direct)
{
    rp_function(c, 0x362C, "pops.emit_known_address_access");
    const uint32_t store = op & 8, load = !store;
    if (guest && load && !(guest & 0x80))
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) & ~(UINT32_C(0x80000000) >> (guest & 31)));
    if (kind == 0) {
        uint32_t index = (address + UINT32_C(0xE07FF000)) >> 3;
        if (index > 0x1FF) index = 0x1FF;
        uint32_t width = op & 7;
        if (width == 3) width = 2;
        out = rp_emit_spill_all(c, out);
        const uint32_t handler = rp_u32(c, c->gp + 0x1000 + index * 8 + (store ? 4 : 0));
        bool call_handler = false;
        if (!store) {
            if (handler == 0x88BC) kind = 3;
            else { address &= 0x1FFF; call_handler = handler != 0x8A54; }
        } else {
            if (handler == 0x89A0) call_handler = true;
            else { address &= 0x1FFF; call_handler = handler != 0x8AA4; }
        }
        if (call_handler) {
            if (!handler) rp_block(c, "compiler_IO_table_not_initialized", address);
            out = emit(c, out, (store ? 0x34060000 : 0x34050000) | (width & 0xFFFF));
            out = rp_emit_constant(c, out, 4, address);
            if (store) out = rp_emit_argument(c, out, 5, guest);
            out = emit(c, out, (handler + UINT32_C(0x30000000)) >> 2);
            out = emit(c, out, 0xAF9901B0);
            out = emit(c, out, 0x8F9901B0);
            invalidate_temporary_names(c);
            return store ? out : rp_emit_result(c, out, guest, direct);
        }
    }
    uint32_t host = guest & 0x7F;
    if (host == guest) {
        int32_t mapped = -1;
        if (op == RP_OP_SW) mapped = rp_emit_lookup_register(c, guest);
        if (op == RP_OP_SW && mapped < -1) {
            const uint32_t last = rp_u32(c, out - 4);
            host = 0u - (uint32_t)mapped;
            if (rp_emit_previous_movable(c, out) &&
                    (last & 0xFFE0FFFF) == 0x44800000 - (uint32_t)mapped * 0x800)
                host = (last >> 16) & 31;
            else op = RP_OP_SWC1;
        } else {
            const uint32_t keep = store || (op & 0x13) == 2;
            const uint32_t allocation = rp_emit_allocate(c, out, guest, 0, 2 * keep + load);
            out = (allocation >> 5) << 2; host = allocation & 31;
        }
    }
    if (kind == 1) {
        const uint32_t resolved = (address & 0x1FFFFF) + 0x09800000;
        uint32_t upper = (resolved >> 16) + ((address & 0x8000) != 0), base = 29;
        if (upper != 0x980) {
            if (store && host == 2) {
                rp_w8(c, c->gp + 0x750, 0);
                out = emit(c, out, 0x3C040000 | (upper & 0xFFFF));
                base = 4;
            } else {
                if (!rp_u32(c, c->gp + 0x744) || rp_u32(c, c->gp + 0x74C) != upper)
                    out = emit(c, out, 0x3C020000 | (upper & 0xFFFF));
                rp_w32(c, c->gp + 0x744, out);
                if (host == 2) { rp_w32(c, c->gp + 0x744, 0); upper = rp_u32(c, c->gp + 0x74C); }
                rp_w32(c, c->gp + 0x74C, upper); base = 2;
            }
        }
        return emit(c, out, (op << 26) | (base << 21) | ((host & 31) << 16) | (resolved & 0xFFFF));
    }
    if (kind == 0 || kind == 2) {
        if (host == 2) rp_w32(c, c->gp + 0x744, 0);
        const uint32_t offset = kind == 0 ? (address & 0xFFF) + 0x12000 :
                                           (address & 0x1FFFFFFF) + UINT32_C(0xE0813000);
        return emit(c, out, (op << 26) | 0x03800000 | ((host & 31) << 16) | (offset & 0xFFFF));
    }
    if (kind == 3 && !store) {
        const uint32_t value = rp_pops_constant_read(c, address & 0x1FFFFFFF, op == RP_OP_LW ? 2 : op & 7);
        out = rp_emit_constant(c, out, host, value);
        rp_w32(c, c->gp + 0xB5C + guest * 4, value);
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) | (UINT32_C(0x80000000) >> (guest & 31)));
    }
    return out;
}

/* +0x3CA8..+0x3FE4: cached stack/scratchpad address specialization. A0 keeps
 * the translated base and GP+0x748 its displacement; branch-likely selects
 * the scratchpad mapping without an out-of-line memory helper.
 */
static uint32_t emit_cached_memory(rp_context *c, uint32_t op, uint32_t guest,
                                   uint32_t base, uint32_t displacement,
                                   uint32_t out, uint32_t direct)
{
    uint32_t offset = sign16(displacement), temporary = 0;
    if (base == byte(c, 0x750) && out != rp_u32(c, c->gp + 0x740)) {
        const uint32_t cached = rp_u32(c, c->gp + 0x748), delta = offset - cached;
        if (delta == sign16(delta)) offset = delta;
        else {
            out = emit(c, out, 0x24840000 | ((0u - cached) & 0xFFFF));
            rp_w32(c, c->gp + 0x748, 0);
        }
    } else {
        int32_t source = rp_emit_lookup_register(c, base);
        if (source < 0) { out = rp_emit_load_register(c, out, 4, base); source = 4; }
        if (source != 4 || offset)
            out = emit(c, out, 0x24040000 | (((uint32_t)source & 31) << 21) | (offset & 0xFFFF));
        temporary = rp_emit_temp(c, 2, 0);
        out = emit(c, out, 0x7C8005C0 | ((temporary & 31) << 16));
        rp_w32(c, c->gp + 0x748, offset);
        rp_w8(c, c->gp + 0x750, (uint8_t)base);
        offset = 0;
    }

    uint32_t host = guest & 0x7F;
    if (host == guest) {
        int32_t floating = 0;
        uint32_t mode;
        if (op & 8) {
            if (op == RP_OP_SW) floating = rp_emit_lookup_register(c, guest);
            mode = 2;
        } else if ((op & 0x13) == 2) {
            mode = 3;
        } else {
            mode = 1;
            if (direct && op == RP_OP_LW && location(c, guest) < -1) {
                for (unsigned i = 0; i < 12; ++i) {
                    if (byte(c, 0x760 + i) != guest) continue;
                    rp_w8(c, c->gp + 0x754 + i, 0);
                    rp_w8(c, c->gp + 0x760 + i, 0);
                    break;
                }
                floating = location(c, guest);
            }
        }
        if (floating < -1) {
            host = (0u - (uint32_t)floating) & 31;
            op = op & 8 ? 0x39 : 0x31; /* Emitted SWC1/LWC1, not a guest opcode. */
        } else {
            const uint32_t allocation = rp_emit_allocate(c, out, guest, 1u << (base & 31), mode);
            host = allocation & 31; out = (allocation >> 5) << 2;
        }
    } else if (host == 2) rp_w32(c, c->gp + 0x744, 0);

    if (temporary) {
        out = emit(c, out, 0x3406004C);
        out = emit(c, out, 0x7CC4FD44);
        out = emit(c, out, 0x54000001 | ((temporary & 31) << 21));
        out = emit(c, out, 0x7CC4FA84);
        rp_emit_release_temp(c, temporary);
    }
    if ((op & 0x11) == 0x10) {
        if (base == 29 && !((rp_u32(c, c->gp + 0x748) + offset) & 3)) {
            out = emit(c, out, (((op & 8) + 0x23) << 26) | 0x00800000 |
                       ((host & 31) << 16) | (offset & 0xFFFF));
        } else {
            out = emit(c, out, (((op & 8) + 0x22) << 26) | 0x00800000 |
                       ((host & 31) << 16) | ((offset + 3) & 0xFFFF));
            out = emit(c, out, (((op & 8) + 0x26) << 26) | 0x00800000 |
                       ((host & 31) << 16) | (offset & 0xFFFF));
        }
    } else if (rp_emit_previous_movable(c, out) && op == RP_OP_LW &&
               (rp_u32(c, out - 4) & 0xFFE0FFFF) == offset + UINT32_C(0xAC800000)) {
        const uint32_t source = (rp_u32(c, out - 4) >> 16) & 31;
        if (source != host) out = emit(c, out, (source << 21) | ((host & 31) << 11) | 0x21);
    } else {
        out = emit(c, out, (op << 26) | 0x00800000 | ((host & 31) << 16) | (offset & 0xFFFF));
    }
    if (guest != base && !(op & 8)) rp_w8(c, c->gp + 0x750, (uint8_t)base);
    return out;
}

/* +0x3FE8..+0x4264: non-specialized dynamic base. Preserve the original
 * argument setup, flush policy, offset folding and selected helper address.
 */
static uint32_t emit_dynamic_memory(rp_context *c, uint32_t op, uint32_t guest,
                                    uint32_t base, uint32_t displacement,
                                    uint32_t out, uint32_t direct)
{
    const bool store = (op & 8) != 0;
    uint32_t offset = sign16(displacement);
    if (!store && guest && !(guest & 0x80))
        rp_w32(c, c->gp + 0xB58, rp_u32(c, c->gp + 0xB58) & ~(0x80000000u >> (guest & 31)));

    if ((base == 29 && !(rp_u32(c, c->gp + 0x6AC) & 0x80000)) ||
            base == byte(c, 0x750) || (op & 0xB) == 0xA ||
            ((op & 0xB) == 2 && out < 0x09B80000))
        return emit_cached_memory(c, op, guest, base, displacement, out, direct);

    bool retargeted = false;
    if (!store && base == guest && (op & 0x13) != 2 && rp_emit_previous_movable(c, out)) {
        const uint32_t last = rp_u32(c, out - 4);
        const int32_t host = rp_emit_lookup_register(c, guest);
        const uint32_t primary = last >> 26;
        if (host >= 0 && ((last & 0xFC00FFC0) == ((uint32_t)host << 11) ||
                (((last >> 16) & 31) == (uint32_t)host &&
                 (primary - 8 < 8 || primary - 0x20 < 7)))) {
            rp_w32(c, out - 4, (primary & 31) ?
                (last & ~UINT32_C(0x1F0000)) | (4u << 16) :
                (last & ~UINT32_C(0xF800)) | (4u << 11));
            retargeted = true;
        }
    }
    if (!retargeted) out = rp_emit_argument(c, out, 4, base);
    if (offset) {
        const uint32_t last = rp_u32(c, out - 4);
        if ((last & 0xFC1FFFFF) == 0x2021) {
            rp_w32(c, out - 4, (last & 0x3E00000) | 0x24040000 | (offset & 0xFFFF));
            offset = 0;
        } else if ((last & 0xFFE0FFFF) == 0x2021) {
            rp_w32(c, out - 4, (((last >> 16) & 31) << 21) | 0x24040000 | (offset & 0xFFFF));
            offset = 0;
        }
    }
    if (store) out = rp_emit_argument(c, out, 5, guest);
    if ((op & 3) != 2) out = rp_emit_flush_registers(c, out, 11);
    if (offset) out = emit(c, out, 0x24840000 | (offset & 0xFFFF));
    uint32_t helper;
    switch (op) {
    case RP_OP_LB: helper = 0x1A90; break;
    case RP_OP_LH: helper = 0x1DE8; break;
    case RP_OP_LW: helper = 0x2128; break;
    case RP_OP_LBU: helper = 0x2468; break;
    case RP_OP_LHU: helper = 0x267C; break;
    case RP_OP_SB: helper = 0x1DD0; break;
    case RP_OP_SH: helper = 0x2110; break;
    case RP_OP_SW: helper = 0x2450; break;
    default: rp_block(c, "dynamic_unaligned_memory_helper_not_reconstructed", op);
    }
    out = rp_emit_jump_delay(c, out, 0x30000000 + helper);
    return store ? out : rp_emit_result(c, out, guest, direct);
}

/* +0x3A90: known-address specialization and generic dynamic helper calls. */
uint32_t rp_emit_memory(rp_context *c, uint32_t op, uint32_t guest, uint32_t base,
                        uint32_t displacement, uint32_t out, uint32_t direct)
{
    rp_function(c, 0x3A90, "pops.emit_memory_access");
    const bool store = (op & 8) != 0;
    if (!store && !(guest & 0x80) && location(c, guest) < 0) {
        for (unsigned i = 0; i < 12; ++i) {
            if (byte(c, 0x760 + i) != guest) continue;
            rp_w8(c, c->gp + 0x754 + i, 0); break;
        }
    }
    if (!known(c, base)) return emit_dynamic_memory(c, op, guest, base, displacement, out, direct);
    const uint32_t address = rp_u32(c, c->gp + 0xB5C + base * 4) + sign16(displacement);
    const uint32_t physical = address & 0x1FFFFFFF;
    const uint32_t kind = (physical >> 23) == 0 ? 1 : address - UINT32_C(0x1F800000) < 0x400 ? 2 :
                          physical + UINT32_C(0xE0400000) < 0x80000 ? 3 : 0;
    uint32_t adjustment = kind == 3 ? (op & 3) + (store ? 15 : 0) :
                          kind == 2 && !store ? UINT32_C(0xFFFFFFFC) : 0;
    if (op & 0x10) adjustment *= 2;
    rp_w32(c, c->gp + 0xB44, rp_u32(c, c->gp + 0xB44) + adjustment);
    if (op & 0x10) {
        if (!(address & 3)) op -= 0x13;
        else {
            const uint32_t first = op & 0x2A;
            out = rp_emit_fixed_memory(c, out, first, guest, kind, address + 3, direct);
            op = first + 4;
        }
    }
    return rp_emit_fixed_memory(c, out, op, guest, kind, address, direct);
}

uint32_t rp_emit_memory_record(rp_context *c, uint32_t record, uint32_t out)
{
    uint16_t flags = half(c, record);
    uint32_t op = *(uint8_t *)rp_memory(c, record + 3, 1);
    const uint32_t base = *(uint8_t *)rp_memory(c, record + 12, 1);
    const uint32_t guest = *(uint8_t *)rp_memory(c, record + 13, 1) | *(uint8_t *)rp_memory(c, record + 2, 1);
    uint32_t displacement = half(c, record + 8), direct = 0;
    const uint32_t next_op = *(uint8_t *)rp_memory(c, record + 19, 1);
    const uint32_t next_base = *(uint8_t *)rp_memory(c, record + 28, 1);
    const bool store = (op & 8) != 0;
    if ((flags & 0x8000) && (op & 3) == 2 && next_op == (op ^ 4) && next_base == base &&
            guest == (*(uint8_t *)rp_memory(c, record + 29, 1) | *(uint8_t *)rp_memory(c, record + 18, 1)) &&
            base != guest) {
        uint32_t high = sign16(half(c, record + 24)), low = sign16(displacement);
        if (!(op & 4)) { high = sign16(displacement); low = sign16(half(c, record + 24)); }
        if (high - 3 == low) {
            op |= 0x34; put_half(c, record + 20, 0); displacement = low;
        }
    } else if (!(flags & 0x8000)) direct = 1;
    else if ((next_op & 0xF0) == 0x20 && (next_op & 3) != 2) {
        if (next_base != 29 || !known(c, 29)) direct = 1;
        else direct = (flags & 0x80) != 0;
    } else direct = (flags & 0x80) != 0;
    if (store && known(c, base) && !(rp_u32(c, c->gp + 0x6AC) & 0x40000)) {
        const uint32_t target = rp_u32(c, c->gp + 0xB5C + base * 4) + sign16(displacement);
        const uint32_t start = rp_u32(c, c->gp + 0xB50);
        if (start + ((record - 0x041B0000) >> 2) < target &&
                target < start + ((rp_u32(c, c->gp + 0xB4C) - 0x041B0000) >> 2)) {
            uint32_t target_record = 0x041B0000 + (target - start) * 4;
            if (half(c, target_record + 4)) {
                if (half(c, target_record) & 1) {
                    target_record -= 16; put_half(c, record, flags - 1);
                }
                put_half(c, target_record + 4, RP_CAT_EXIT);
                put_half(c, target_record, half(c, target_record) | 0x10);
            }
        }
    }
    out = rp_emit_memory(c, op, guest, base, displacement, out, direct);
    if (store && ((next_op & 0xF8) == 0x20 || half(c, c->gp + 0xB42)))
        rp_w32(c, c->gp + 0xB44, rp_u32(c, c->gp + 0xB44) + 2);
    return out;
}

/* +0x46A0's ordinary GP-relative state writes. Special GTE destinations and
 * the cause-byte merge remain separate from this store policy path. */
static uint32_t emit_plain_state(rp_context *c, uint32_t policy, uint32_t guest,
                                 uint32_t offset, uint32_t out)
{
    if (offset == 0x134 || offset == 0x3C || offset == 0x70 || offset == 0x78)
        rp_block(c, "special_state_write_destination_not_reconstructed", offset);
    if (guest & 0x80)
        return emit(c, out, 0xAF800000 | ((guest & 31) << 16) | (offset & 0xFFFF));
    if (policy > 2) return out;
    if (policy == 1) {
        const uint32_t value = rp_u32(c, c->gp + 0xB5C + guest * 4);
        if (!known(c, guest) || value != sign16(value))
            rp_block(c, "signed_half_state_policy_not_reconstructed", 0x4930);
    }
    if (policy != 2) {
        const int32_t mapped = rp_emit_lookup_register(c, guest);
        if (mapped < -1) {
            const uint32_t previous = rp_u32(c, out - 4), fpr = (0u - (uint32_t)mapped) & 31;
            if (rp_emit_previous_movable(c, out) &&
                    (previous & UINT32_C(0xFFE0FFFF)) == (0x44800000 | (fpr << 11)))
                return emit(c, out, 0xAF800000 | (previous & 0x1F0000) | (offset & 0xFFFF));
            return emit(c, out, 0xE7800000 | (fpr << 16) | (offset & 0xFFFF));
        }
    }
    const uint32_t allocation = rp_emit_allocate(c, out, guest, 0, 2);
    return emit(c, (allocation >> 5) << 2,
                (policy == 2 ? 0xA7800000 : 0xAF800000) |
                ((allocation & 31) << 16) | (offset & 0xFFFF));
}

/* CPU status has an explicit mask and an interrupt-deadline helper call. */
uint32_t rp_emit_store_state(rp_context *c, uint32_t policy, uint32_t guest,
                             uint32_t offset, uint32_t out)
{
    rp_function(c, 0x46A0, "pops.emit_CPU_status_write_partial");
    policy &= 0xFF;
    if ((policy & 0xFF) == 3) return out;
    if (offset == 0x134) {
        uint32_t host = guest & 0x7F;
        if (host == guest) {
            const uint32_t allocation = rp_emit_allocate(c, out, guest, 0, 2);
            host = allocation & 31;
            out = (allocation >> 5) << 2;
        }
        out = emit(c, out, 0x83850135); /* LB A1, software-pending byte */
        if (host) out = emit(c, out, 0x7C060A00 | ((host & 31) << 21));
        out = emit(c, out, 0x7C050804 | (host ? 6u << 21 : 0));
        out = emit(c, out, 0xA3850135);
        if (!host) return out;
        out = rp_emit_flush_registers(c, out, 11);
        out = emit(c, out, 0x0C0025AB);
        out = emit(c, out, 0xAF9901B0);
        return emit(c, out, 0x8F9901B0);
    }
    if (offset != 0x130) return emit_plain_state(c, policy, guest, offset, out);
    uint32_t host = guest & 0x7F;
    if (host == guest) {
        const uint32_t allocation = rp_emit_allocate(c, out, guest, 0, 2);
        out = (allocation >> 5) << 2;
        host = allocation & 31;
    }
    if (guest == 0x80) guest = 0;
    const uint32_t mask = UINT32_C(0xF27DFF3F);
    if (!(guest & 0x80) && known(c, guest)) {
        const uint32_t value = rp_u32(c, c->gp + 0xB5C + guest * 4);
        if (!(value & 1)) {
            if ((value & mask) != value) {
                out = rp_emit_constant(c, out, 5, value & mask);
                host = 5;
            }
            return emit(c, out, 0xAF800130 | ((host & 31) << 16));
        }
    }
    out = rp_emit_constant(c, out, 5, mask);
    out = emit(c, out, 0x00052824 | ((host & 31) << 21));
    out = emit(c, out, 0xAF850130);
    out = rp_emit_flush_registers(c, out, 11);
    out = emit(c, out, 0x0C0025AB);
    out = emit(c, out, 0xAF9901B0);
    return emit(c, out, 0x8F9901B0);
}
