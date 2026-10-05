/* Historical experiment: not linked into the native executable (Unicorn is).
 * Provisional backend, NOT reconstructed Sony code or an optimized recompiler.
 * It only executes the cache produced by our C emitter. PRX helper addresses
 * must return to explicit native reconstructions in pops_dispatch.c.
 */
#include "runtime.h"

static uint32_t sx16(uint32_t value)
{
    return ((value & 0xFFFF) ^ UINT32_C(0x8000)) - UINT32_C(0x8000);
}
static uint32_t asr(uint32_t value, unsigned bits)
{
    bits &= 31;
    if (!bits) return value;
    return (value >> bits) | ((value & UINT32_C(0x80000000)) ? (~UINT32_C(0) << (32 - bits)) : 0);
}
static uint32_t read_value(rp_context *c, uint32_t address, unsigned bytes)
{
    if (address & (bytes - 1)) rp_block(c, "generated_unaligned_read", address);
    const uint8_t *p = rp_memory(c, address, bytes);
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint32_t)p[i] << (8 * i);
    return value;
}
static void write_value(rp_context *c, uint32_t address, uint32_t value, unsigned bytes)
{
    if (address & (bytes - 1)) rp_block(c, "generated_unaligned_write", address);
    uint8_t *p = rp_memory(c, address, bytes);
    for (unsigned i = 0; i < bytes; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

void rp_generated_step(rp_context *c)
{
    const uint32_t pc = c->run_pc;
    if (pc < 0x09B80000 || pc >= rp_u32(c, c->gp + 0x1D0) || (pc & 3))
        rp_block(c, "execution_outside_reconstructed_code_cache", pc);
    const uint32_t word = rp_u32(c, pc), op = word >> 26;
    const unsigned rs = (word >> 21) & 31, rt = (word >> 16) & 31;
    const unsigned rd = (word >> 11) & 31, sa = (word >> 6) & 31;
    uint32_t *r = c->run_gpr;
    const uint32_t immediate = sx16(word), next = c->run_next_pc;
    uint32_t following = next + 4;
    const uint32_t branch_target = pc + 4 + (immediate << 2);
    r[0] = 0;
    switch (op) {
    case 0:
        switch (word & 63) {
        case 0: r[rd] = r[rt] << sa; break;
        case 2:
            if (rs != 0) goto unsupported;
            r[rd] = r[rt] >> sa; break;
        case 3: r[rd] = asr(r[rt], sa); break;
        case 4: r[rd] = r[rt] << (r[rs] & 31); break;
        case 6: r[rd] = r[rt] >> (r[rs] & 31); break;
        case 7: r[rd] = asr(r[rt], r[rs]); break;
        case 8: following = r[rs]; break;
        case 9: following = r[rs]; r[rd] = pc + 8; break;
        case 10: if (!r[rt]) r[rd] = r[rs]; break;
        case 11: if (r[rt]) r[rd] = r[rs]; break;
        case 16: r[rd] = c->run_hi; break;
        case 17: c->run_hi = r[rs]; break;
        case 18: r[rd] = c->run_lo; break;
        case 19: c->run_lo = r[rs]; break;
        case 0x21: r[rd] = r[rs] + r[rt]; break;
        case 0x23: r[rd] = r[rs] - r[rt]; break;
        case 0x24: r[rd] = r[rs] & r[rt]; break;
        case 0x25: r[rd] = r[rs] | r[rt]; break;
        case 0x26: r[rd] = r[rs] ^ r[rt]; break;
        case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
        case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;
        case 0x2B: r[rd] = r[rs] < r[rt]; break;
        default: goto unsupported;
        }
        break;
    case 1:
        if (rt == 0) { if ((int32_t)r[rs] < 0) following = branch_target; }
        else if (rt == 1) { if ((int32_t)r[rs] >= 0) following = branch_target; }
        else goto unsupported;
        break;
    case 2: case 3:
        following = ((pc + 4) & UINT32_C(0xF0000000)) | ((word & 0x3FFFFFF) << 2);
        if (op == 3) r[31] = pc + 8;
        break;
    case 4: if (r[rs] == r[rt]) following = branch_target; break;
    case 5: if (r[rs] != r[rt]) following = branch_target; break;
    case 6: if ((int32_t)r[rs] <= 0) following = branch_target; break;
    case 7: if ((int32_t)r[rs] > 0) following = branch_target; break;
    case 9: r[rt] = r[rs] + immediate; break;
    case 10: r[rt] = (int32_t)r[rs] < (int32_t)immediate; break;
    case 11: r[rt] = r[rs] < immediate; break;
    case 12: r[rt] = r[rs] & (word & 0xFFFF); break;
    case 13: r[rt] = r[rs] | (word & 0xFFFF); break;
    case 14: r[rt] = r[rs] ^ (word & 0xFFFF); break;
    case 15: r[rt] = word << 16; break;
    case 17:
        if (rs == 0) r[rt] = c->run_fpr[rd];
        else if (rs == 4) c->run_fpr[rd] = r[rt];
        else goto unsupported;
        break;
    case 0x1F:
        if ((word & 63) == 0) {
            const unsigned bits = rd + 1;
            if (sa + bits > 32) goto unsupported;
            const uint32_t mask = bits == 32 ? UINT32_MAX : (1u << bits) - 1;
            r[rt] = (r[rs] >> sa) & mask;
        } else if ((word & 63) == 4) {
            if (rd < sa) goto unsupported;
            const unsigned bits = rd - sa + 1;
            const uint32_t mask = bits == 32 ? UINT32_MAX : ((1u << bits) - 1) << sa;
            r[rt] = (r[rt] & ~mask) | ((r[rs] << sa) & mask);
        } else goto unsupported;
        break;
    case 0x20: r[rt] = (read_value(c, r[rs] + immediate, 1) ^ 0x80) - 0x80; break;
    case 0x21: r[rt] = sx16(read_value(c, r[rs] + immediate, 2)); break;
    case 0x23: r[rt] = read_value(c, r[rs] + immediate, 4); break;
    case 0x24: r[rt] = read_value(c, r[rs] + immediate, 1); break;
    case 0x25: r[rt] = read_value(c, r[rs] + immediate, 2); break;
    case 0x28: write_value(c, r[rs] + immediate, r[rt], 1); break;
    case 0x29: write_value(c, r[rs] + immediate, r[rt], 2); break;
    case 0x2B: write_value(c, r[rs] + immediate, r[rt], 4); break;
    case 0x31: c->run_fpr[rt] = read_value(c, r[rs] + immediate, 4); break;
    case 0x39: write_value(c, r[rs] + immediate, c->run_fpr[rt], 4); break;
    default: goto unsupported;
    }
    r[0] = 0;
    c->run_pc = next;
    c->run_next_pc = following;
    ++c->generated_instructions;
    return;
unsupported:
    rp_event(c, "execution_adapter_boundary", "unsupported_generated_word", pc, word);
    rp_block(c, "generated_instruction_not_supported", pc);
}
