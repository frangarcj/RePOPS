#include "runtime.h"
#include "pops_ir.h"

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
 * Recovered enum names preserve every numeric field in the original records.
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
        uint16_t kind = RP_CAT_EMPTY;
        uint32_t cost = 1;
        if (pc == (rp_u32(c, c->gp + 0x6F4) & UINT32_C(0xFFFFFFFE))) flags |= 0x1000;
        if (op == RP_OP_SPECIAL) op = (instruction & 63) | 0x40;

        switch (op) {
        case RP_OP_REGIMM:
            if (rt & 0xE) goto unsupported;
            if (rt & 0x10) rp_w8(c, p + 2, 31);
            op = (rt & 1) ^ 3;
            rt = 0; op += 0xBE;
            break;
        case RP_OP_J: rs = 0; rt = 0; break;
        case RP_OP_JAL: rp_w8(c, p + 2, 31); rs = 0; rt = 0; break;
        case RP_OP_RAW_BLEZ: case RP_OP_RAW_BGTZ: rt = 0; op += 0xBE; break;
        case RP_OP_RAW_BEQ: case RP_OP_RAW_BNE: op += 0xBE; break;
        case RP_OP_LUI: rs = 0; /* fall through */
        case RP_OP_ADDI: op |= 1; /* fall through */
        case RP_OP_ADDIU: case RP_OP_SLTI: case RP_OP_SLTIU: case RP_OP_ANDI: case RP_OP_ORI: case RP_OP_XORI:
            kind = rt ? RP_CAT_IMMEDIATE : RP_CAT_ELIDED; rp_w8(c, p + 2, (uint8_t)rt); rt = 0; break;
        case RP_OP_COP0:
            rp_w8(c, p + 14, (uint8_t)(rd + 0x40));
            if (rs == 4) {
                kind = RP_CAT_WRITE_COP;
                if (rd == 12 && !(flags & 1) && ((pc & 0x7FFFFFFF) >> 29) != 0) {
                    wh(c, p + 20, RP_CAT_EXIT); flags_or(c, p + 16, 0x10);
                }
            } else if (rs == 0) {
                kind = rt ? RP_CAT_READ_COP : RP_CAT_ELIDED; rp_w8(c, p + 2, (uint8_t)rt); rt = 0;
            } else if (rs == 0x10) {
                kind = RP_CAT_COP0_CONTROL; rt = 0;
            } else {
                kind = RP_CAT_ELIDED; rt = 0; rp_w8(c, p + 2, 0);
            }
            rs = 0;
            break;
        case RP_OP_COP2:
            cost = 3;
            if (rs == 0 || rs == 2) {
                if (rs == 2) rd += 32;
                kind = rt ? RP_CAT_READ_COP : RP_CAT_ELIDED;
                if (rt) flags |= 0x40;
                rp_w8(c, p + 2, rt ? (uint8_t)rt : 0xFF);
                rt = 0;
            } else if (rs == 4 || rs == 6) {
                if (rs == 6) rd += 32;
                kind = RP_CAT_WRITE_COP;
            } else {
                rt = 0;
                if (rs & 0x10) {
                    cost = *(const uint8_t *)rp_module_memory(c, 0xD46C4 + (instruction & 63), 1);
                    kind = RP_CAT_GTE;
                    if (cost != 1) {
                        flags |= 0x4000;
                        rp_w8(c, p + 2, 0xFF);
                        rs = 0; rp_w8(c, p + 14, (uint8_t)rd);
                        break;
                    }
                }
                rp_w8(c, p + 2, 0); kind = RP_CAT_ELIDED; cost = 1;
            }
            rs = 0; rp_w8(c, p + 14, (uint8_t)rd);
            break;
        case RP_OP_LB: case RP_OP_LH: case RP_OP_LWL: case RP_OP_LW: case RP_OP_LBU: case RP_OP_LHU: case RP_OP_LWR:
            rp_w8(c, p + 2, (uint8_t)rt);
            if (rt) flags |= 0x40;
            kind = RP_CAT_MEMORY; cost = 5; rt = 0;
            break;
        case RP_OP_SB: case RP_OP_SH: case RP_OP_SWL: case RP_OP_SW: case RP_OP_SWR:
            kind = RP_CAT_MEMORY;
            if (!(rp_u32(c, c->gp + 0x130) & 0x10000)) break;
            /* fall through */
        case RP_OP_LWC0: case RP_OP_SWC0:
            kind = RP_CAT_ELIDED; rs = 0; rt = 0; rp_w8(c, p + 2, 0);
            break;
        case RP_OP_LWC2: case RP_OP_SWC2:
            rp_w8(c, p + 14, (uint8_t)rt); rt = 0;
            kind = op == 0x32 ? RP_CAT_LOAD_COP_MEMORY : RP_CAT_STORE_COP_MEMORY; cost = kind;
            break;
        case RP_OP_SLL: case RP_OP_SRL: case RP_OP_SRA:
            kind = rd ? RP_CAT_SHIFT_IMMEDIATE : RP_CAT_ELIDED; rp_w8(c, p + 2, (uint8_t)rd); break;
        case RP_OP_ADD: case RP_OP_SUB: op |= 1; /* fall through */
        case RP_OP_SLLV: case RP_OP_SRLV: case RP_OP_SRAV: case RP_OP_ADDU: case RP_OP_SUBU:
        case RP_OP_AND: case RP_OP_OR: case RP_OP_XOR: case RP_OP_NOR: case RP_OP_SLT: case RP_OP_SLTU:
            kind = rd ? RP_CAT_ALU : RP_CAT_ELIDED; rp_w8(c, p + 2, (uint8_t)rd); break;
        case RP_OP_JALR: rp_w8(c, p + 2, (uint8_t)rd); /* fall through */
        case RP_OP_JR:
            if (h(c, p + 20) == 0) {
                if (!(h(c, c->gp + 0xB40) & 2) && b(c, p + 2) != 0)
                    flags_or(c, p + 32, 8);
                else flags_or(c, p + 16, 0x10);
            }
            flags_or(c, p + 16, 1); kind = RP_CAT_JUMP_REGISTER; rt = 0;
            break;
        case RP_OP_SYSCALL: case RP_OP_BREAK:
            kind = RP_CAT_EXCEPTION; flags |= 0x10; rs = 0; rt = 0; break;
        case RP_OP_MFHI: case RP_OP_MFLO:
            kind = rd ? RP_CAT_READ_HILO : RP_CAT_ELIDED; rs = 0; rt = 0;
            rp_w8(c, p + 2, (uint8_t)rd); break;
        case RP_OP_MTHI: case RP_OP_MTLO: kind = RP_CAT_WRITE_HILO; rt = 0; break;
        case RP_OP_MULT: case RP_OP_MULTU: kind = RP_CAT_MULT_DIV; cost = 12; break;
        case RP_OP_DIV: case RP_OP_DIVU: kind = RP_CAT_MULT_DIV; cost = 35; break;
        default:
unsupported:
            kind = pc == base_pc ? RP_CAT_EXCEPTION : RP_CAT_EXIT;
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
                wh(c, p + 36, RP_CAT_EXIT); flags_or(c, p + 32, 0x10);
            }
            rp_w32(c, p + 8, target);
            uint32_t recurse = 0;
            if (target == pc + 8 && b(c, p + 2) == 0) {
                wh(c, p + 4, RP_CAT_ELIDED); rp_w8(c, p + 2, 0);
            } else if (h(c, p + 20) != 0 && conditional) {
                wh(c, p + 4, RP_CAT_EXIT); flags_or(c, p, 0x10);
            } else {
                if (!(h(c, c->gp + 0xB40) & 2) && b(c, p + 2))
                    flags_or(c, p + 32, 8);
                else if (!conditional && !(h(c, p + 16) & 8))
                    flags_or(c, p + 16, 0x10);
                wh(c, p + 4, conditional ? RP_CAT_BRANCH : RP_CAT_JUMP_DIRECT);
                flags_or(c, p + 16, 1);
                if (!(h(c, c->gp + 0xB40) & 8) && target >= base_pc && target < ceiling &&
                        !cached_entry(c, target)) {
                    const uint32_t destination = RECORD_BASE + (target - base_pc) * 4;
                    const uint16_t destination_flags = h(c, destination);
                    if (!((destination_flags & 1) && h(c, destination - 12) == RP_CAT_BRANCH) &&
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
            if (!(h(c, p) & 1)) { wh(c, p + 4, RP_CAT_EXIT); flags_or(c, p, 0x10); }
        }
        if (h(c, p + 4)) break;
    }

    /* The backwards annotation starts two records before the terminal entry. */
    if (p >= RECORD_BASE + 32) {
        uint32_t back = p - 32;
        for (;;) {
            const uint16_t kind = h(c, back + RP_CAT_STORE_COP_MEMORY);
            if (kind == RP_CAT_EMPTY) break;
            if (kind != RP_CAT_MEMORY && kind != RP_CAT_ELIDED && h(c, back + 10) != 0x27BD &&
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
