#include "runtime.h"

#define RECORD_BASE UINT32_C(0x041B0000)

static uint16_t h(rp_context *c, uint32_t p)
{
    const uint8_t *b = rp_memory(c, p, 2);
    return (uint16_t)(b[0] | (uint16_t)b[1] << 8);
}
static uint8_t b(rp_context *c, uint32_t p)
{
    return *(uint8_t *)rp_memory(c, p, 1);
}
static void wh(rp_context *c, uint32_t p, uint16_t v)
{
    uint8_t *bytes = rp_memory(c, p, 2);
    bytes[0] = (uint8_t)v; bytes[1] = (uint8_t)(v >> 8);
}
static void flags_or(rp_context *c, uint32_t p, uint16_t flags)
{
    wh(c, p, (uint16_t)(h(c, p) | flags));
}

/* Same cache-table query used by +0x05154; this does not execute a block. */
static bool cached_entry(rp_context *c, uint32_t pc)
{
    const uint32_t physical = pc & UINT32_C(0x1FFFFFFF);
    if ((physical >> 23) == 0)
        return rp_u32(c, 0x09C00000 + (pc & 0x1FFFFC)) != 0;
    if ((physical + UINT32_C(0xE0400000)) < 0x80000)
        return rp_u32(c, 0x09E00000 + (physical & 0x1FFFFC)) != 0;
    return false;
}

/* Reconstructed +0x05154. Keep the original 16-byte record format and its
 * in-place flags/recursive target discovery; no host backend is chosen here.
 * Type names are deliberately numeric until the emitter is fully recovered.
 */
static void analyze(rp_context *c, uint32_t p, unsigned depth)
{
    rp_function(c, 0x5154, "pops.analyze_instruction_records");
    if (depth > 256) rp_block(c, "analysis_recursion_host_limit", p);
    const uint32_t base_pc = rp_u32(c, c->gp + 0xB50);
    uint32_t pc = base_pc + ((p - RECORD_BASE) >> 2);
    const uint32_t ceiling = rp_u32(c, c->gp + 0xB54);
    uint32_t limit = pc + 0x1000;
    if (ceiling < limit) limit = ceiling;

    for (;;) {
        if (p < RECORD_BASE || p > RECORD_BASE + 0xFFF0)
            rp_block(c, "analysis_record_buffer_limit", p);
        uint16_t flags = h(c, p);
        if (flags & 9) {
            limit = pc + 0x1000;
            if (ceiling < limit) limit = ceiling;
        }
        const uint32_t source = rp_u32(c, c->gp + 0xB48) + pc;
        const uint32_t instruction = rp_u32(c, source);
        rp_w32(c, p + 8, instruction);
        uint32_t op = instruction >> 26;
        uint32_t rs = (instruction >> 21) & 31, rt = (instruction >> 16) & 31;
        uint32_t rd = (instruction >> 11) & 31;
        uint16_t kind = 0;
        uint32_t cost = 1;
        if (pc == (rp_u32(c, c->gp + 0x6F4) & UINT32_C(0xFFFFFFFE))) flags |= 0x1000;
        if (op == 0) op = (instruction & 63) | 0x40;

        switch (op) {
        case 1:
            if (rt & 0xE) goto unsupported;
            if (rt & 0x10) rp_w8(c, p + 2, 31);
            op = (rt & 1) ^ 3;
            rt = 0; op += 0xBE;
            break;
        case 2: rs = 0; rt = 0; break;
        case 3: rp_w8(c, p + 2, 31); rs = 0; rt = 0; break;
        case 6: case 7: rt = 0; op += 0xBE; break;
        case 4: case 5: op += 0xBE; break;
        case 0xF: rs = 0; /* fall through */
        case 8: op |= 1; /* fall through */
        case 9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE:
            kind = rt ? 9 : 0x13; rp_w8(c, p + 2, (uint8_t)rt); rt = 0; break;
        case 0x10:
            rp_w8(c, p + 14, (uint8_t)(rd + 0x40));
            if (rs == 4) {
                kind = 10;
                if (rd == 12 && !(flags & 1) && ((pc & 0x7FFFFFFF) >> 29) != 0) {
                    wh(c, p + 20, 5); flags_or(c, p + 16, 0x10);
                }
            } else if (rs == 0) {
                kind = rt ? 6 : 0x13; rp_w8(c, p + 2, (uint8_t)rt); rt = 0;
            } else if (rs == 0x10) {
                kind = 7; rt = 0;
            } else {
                kind = 0x13; rt = 0; rp_w8(c, p + 2, 0);
            }
            rs = 0;
            break;
        case 0x12:
            cost = 3;
            if (rs == 0 || rs == 2) {
                if (rs == 2) rd += 32;
                kind = rt ? 6 : 0x13;
                if (rt) flags |= 0x40;
                rp_w8(c, p + 2, rt ? (uint8_t)rt : 0xFF);
                rt = 0;
            } else if (rs == 4 || rs == 6) {
                if (rs == 6) rd += 32;
                kind = 10;
            } else {
                rt = 0;
                if (rs & 0x10) {
                    cost = *(const uint8_t *)rp_module_memory(c, 0xD46C4 + (instruction & 63), 1);
                    kind = 0x12;
                    if (cost != 1) {
                        flags |= 0x4000;
                        rp_w8(c, p + 2, 0xFF);
                        rs = 0; rp_w8(c, p + 14, (uint8_t)rd);
                        break;
                    }
                }
                rp_w8(c, p + 2, 0); kind = 0x13; cost = 1;
            }
            rs = 0; rp_w8(c, p + 14, (uint8_t)rd);
            break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
            rp_w8(c, p + 2, (uint8_t)rt);
            if (rt) flags |= 0x40;
            kind = 0x10; cost = 5; rt = 0;
            break;
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E:
            kind = 0x10;
            if (!(rp_u32(c, c->gp + 0x130) & 0x10000)) break;
            /* fall through */
        case 0x30: case 0x38:
            kind = 0x13; rs = 0; rt = 0; rp_w8(c, p + 2, 0);
            break;
        case 0x32: case 0x3A:
            rp_w8(c, p + 14, (uint8_t)rt); rt = 0;
            kind = op == 0x32 ? 8 : 4; cost = kind;
            break;
        case 0x40: case 0x42: case 0x43:
            kind = rd ? 0x11 : 0x13; rp_w8(c, p + 2, (uint8_t)rd); break;
        case 0x60: case 0x62: op |= 1; /* fall through */
        case 0x44: case 0x46: case 0x47: case 0x61: case 0x63:
        case 0x64: case 0x65: case 0x66: case 0x67: case 0x6A: case 0x6B:
            kind = rd ? 0xD : 0x13; rp_w8(c, p + 2, (uint8_t)rd); break;
        case 0x49: rp_w8(c, p + 2, (uint8_t)rd); /* fall through */
        case 0x48:
            if (h(c, p + 20) == 0) {
                if (!(h(c, c->gp + 0xB40) & 2) && b(c, p + 2) != 0)
                    flags_or(c, p + 32, 8);
                else flags_or(c, p + 16, 0x10);
            }
            flags_or(c, p + 16, 1); kind = 1; rt = 0;
            break;
        case 0x4C: case 0x4D:
            kind = 0xF; flags |= 0x10; rs = 0; rt = 0; break;
        case 0x50: case 0x52:
            kind = rd ? 2 : 0x13; rs = 0; rt = 0;
            rp_w8(c, p + 2, (uint8_t)rd); break;
        case 0x51: case 0x53: kind = 0xB; rt = 0; break;
        case 0x58: case 0x59: kind = 3; cost = 12; break;
        case 0x5A: case 0x5B: kind = 3; cost = 35; break;
        default:
unsupported:
            kind = pc == base_pc ? 0xF : 5;
            flags |= 0x10; rs = 0; rt = 0;
            break;
        }

        wh(c, p, flags);
        rp_w8(c, p + 13, (uint8_t)rt); rp_w8(c, p + 12, (uint8_t)rs);
        rp_w8(c, p + 3, (uint8_t)op);
        rp_w8(c, p + 15, (uint8_t)(b(c, c->gp + 0xB42) + cost));
        if (kind) wh(c, p + 4, kind);
        else {
            const int32_t immediate = (int32_t)(instruction & 0xFFFF) -
                                      ((instruction & 0x8000) ? 0x10000 : 0);
            const uint32_t target = op < 0xC0 ?
                (pc & 0xF0000000) + (instruction & 0x3FFFFFF) * 4 :
                pc + 4 + (uint32_t)immediate * 4;
            const bool conditional = op >= 0xC0 && ((op & 1) || rs != rt);
            if (conditional && cached_entry(c, pc + 8)) {
                wh(c, p + 36, 5); flags_or(c, p + 32, 0x10);
            }
            rp_w32(c, p + 8, target);
            uint32_t recurse = 0;
            if (target == pc + 8 && b(c, p + 2) == 0) {
                wh(c, p + 4, 0x13); rp_w8(c, p + 2, 0);
            } else if (h(c, p + 20) != 0 && conditional) {
                wh(c, p + 4, 5); flags_or(c, p, 0x10);
            } else {
                if (!(h(c, c->gp + 0xB40) & 2) && b(c, p + 2))
                    flags_or(c, p + 32, 8);
                else if (!conditional && !(h(c, p + 16) & 8))
                    flags_or(c, p + 16, 0x10);
                wh(c, p + 4, conditional ? 0xC : 0xE);
                flags_or(c, p + 16, 1);
                if (!(h(c, c->gp + 0xB40) & 8) && target >= base_pc && target < ceiling &&
                        !cached_entry(c, target)) {
                    const uint32_t destination = RECORD_BASE + (target - base_pc) * 4;
                    const uint16_t destination_flags = h(c, destination);
                    if (!((destination_flags & 1) && h(c, destination - 12) == 0xC) &&
                            !(destination_flags & 0x10)) {
                        flags_or(c, p, 4);
                        if (destination_flags & 8) wh(c, destination, destination_flags & 0xFCFF);
                        else {
                            wh(c, destination, destination < p ? destination_flags | 0x108 : 0x208);
                            if (h(c, destination + 4) == 0) recurse = destination;
                        }
                    }
                }
            }
            if (recurse) analyze(c, recurse, depth + 1);
        }
        if (h(c, p) & 0x10) break;
        pc += 4; p += 16;
        if (pc >= limit || (((pc & 0x1FFFFFFF) >> 23) == 0 && cached_entry(c, pc))) {
            if (!(h(c, p) & 1)) { wh(c, p + 4, 5); flags_or(c, p, 0x10); }
        }
        if (h(c, p + 4)) break;
    }

    /* The backwards annotation starts two records before the terminal entry. */
    if (p >= RECORD_BASE + 32) {
        uint32_t back = p - 32;
        for (;;) {
            const uint16_t kind = h(c, back + 4);
            if (kind == 0) break;
            if (kind != 0x10 && kind != 0x13 && h(c, back + 10) != 0x27BD &&
                    h(c, back + 10) != 0x2402) break;
            flags_or(c, back, 0x80);
            if (back == RECORD_BASE) break;
            back -= 16;
        }
    }
    if (rp_u32(c, c->gp + 0xB4C) < p) rp_w32(c, c->gp + 0xB4C, p);
}

void rp_pops_analyze_records(rp_context *c, uint32_t first_record)
{
    analyze(c, first_record, 0);
}
