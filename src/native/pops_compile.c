#include "runtime.h"

#define RECORD_BASE UINT32_C(0x041B0000)

static uint16_t h(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint8_t b(rp_context *c, uint32_t address)
{
    return *(uint8_t *)rp_memory(c, address, 1);
}
static void wh(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* +0x5A68..+0x5D5B: refine per-operation costs and place accumulated costs
 * at boundaries. This is recovered accounting, not a host execution engine.
 */
static void account_costs(rp_context *c, uint32_t end)
{
    const uint32_t baseline = h(c, c->gp + 0xB42);
    uint32_t region = RECORD_BASE, accumulated = 0, instructions = 0;
    for (uint32_t p = RECORD_BASE; p < end; p += 16) {
        if (h(c, p + 4) == 0) {
            if (instructions == 0) region = p;
            continue;
        }
        uint16_t flags = h(c, p);
        if (!(flags & 0x11) && !(h(c, p + 16) & 8)) {
            flags |= 0x8000; wh(c, p, flags);
        }
        uint32_t cost = b(c, p + 15);
        ++instructions;
        if (cost > baseline + 1 && !(flags & 1)) {
            const uint32_t opcode = b(c, p + 3);
            int32_t remaining = (int32_t)(cost - baseline);
            if (opcode - UINT32_C(0x20) < 7 && baseline == 0) {
                const uint8_t dest = b(c, p + 2);
                for (uint32_t q = p + 16; q < end; q += 16) {
                    if (h(c, q + 4) == 0x10) break;
                    if (q != p + 16 && (dest == b(c, q + 12) || dest == b(c, q + 13))) break;
                    remaining -= b(c, q + 15);
                    if (remaining < 2) { remaining = 1; break; }
                    if (h(c, q) & 1) { remaining >>= 1; break; }
                }
            } else if (flags & 0x4000) {
                for (uint32_t q = p;; q -= 16) {
                    const uint16_t prior_flags = h(c, q);
                    if (prior_flags & 0x2000) break;
                    wh(c, q, prior_flags | 0x2000);
                    if (q == RECORD_BASE) break;
                    const uint8_t prior_dest = b(c, q - 14);
                    if (prior_dest > 0 && prior_dest < 0x80 && b(c, q - 2) == 0x3F) break;
                    if (h(c, q - 16) & 0x11) break;
                }
                for (uint32_t q = p + 16; q < end; q += 16) {
                    const uint8_t next_op = b(c, q + 3);
                    if ((next_op == 0x12 && b(c, q + 2) != 0) || next_op == 0x3A) break;
                    remaining -= b(c, q + 15);
                    if (remaining < 2) { remaining = 1; break; }
                    if (h(c, q) & 1) { remaining >>= 1; break; }
                }
            } else if (opcode - UINT32_C(0x58) < 4) {
                for (uint32_t q = p + 16; q < end; q += 16) {
                    const uint32_t next_op = b(c, q + 3);
                    if (next_op == 0x50 || next_op == 0x52) break;
                    remaining -= b(c, q + 15);
                    if (next_op - UINT32_C(0x58) < 4 || remaining < 2) {
                        remaining = 1; break;
                    }
                    if (h(c, q) & 1) { remaining >>= 1; break; }
                }
            }
            if (rp_u32(c, c->gp + 0x6AC) & 0x10000000) remaining = 1;
            cost = (uint32_t)remaining + baseline;
        }
        accumulated += cost;
        if (!(flags & 0x19)) continue;
        if (!(flags & 8)) cost = 0;
        if (baseline == 0) {
            const uint32_t target_record = RECORD_BASE +
                (rp_u32(c, p + 8) - rp_u32(c, c->gp + 0xB50)) * 4;
            if (((flags & 1) && (!(flags & 4) || target_record <= p) && p - region > 0x4000) ||
                    (flags & 0x10)) {
                instructions >>= b(c, c->gp + 0x1C2) & 31;
                if (region == RECORD_BASE) rp_w32(c, c->gp + 0xB44, instructions);
                else accumulated += instructions;
                region = p + 16; instructions = 0;
            }
        }
        wh(c, p + 6, (uint16_t)(h(c, p + 6) + accumulated - cost));
        accumulated = cost;
    }
}

/* +0x58C0 through +0x5D5B. Returns the proposed emission cursor, NOT an
 * executable block. Exception dispatch and RAM cache rollover are explicit
 * boundaries; the later allocator/emitter/linker are not run by this prefix.
 */
uint32_t rp_pops_prepare_compile(rp_context *c, uint32_t pc)
{
    rp_function(c, 0x58C0, "pops.compile_analysis_and_cost_prefix");
    const uint32_t physical = pc & UINT32_C(0x1FFFFFFF);
    if ((physical >> 23) != 0 && physical + UINT32_C(0xE0400000) > 0x7FFFF) {
        rp_w32(c, c->gp + 0x120, pc);
        rp_block(c, "compiler_exception_dispatch_not_reconstructed", 0x94C4);
    }
    uint32_t output, baseline;
    uint16_t flags;
    if ((physical >> 23) == 0) {
        rp_w32(c, c->gp + 0xB48, UINT32_C(0x09800000) - (pc & UINT32_C(0xFFE00000)));
        output = rp_u32(c, c->gp + 0x1CC);
        if (output > 0x097B0004) rp_block(c, "RAM_code_cache_rollover_not_reconstructed", 0x7E60);
        uint32_t config = rp_u32(c, c->gp + 0x6B0);
        if (config == UINT32_MAX) config = 3;
        flags = (uint16_t)(pc == rp_u32(c, c->gp + 0x6E4) ? 0x4007 : config);
        baseline = ((pc & 0x7FFFFFFF) >> 29) == 0 ? 0 : 5;
    } else {
        rp_w32(c, c->gp + 0xB48, UINT32_C(0x00053C20) - (pc & UINT32_C(0xFFF80000)));
        flags = 0x8000; baseline = 2;
        output = rp_u32(c, c->gp + 0x1D0);
    }
    wh(c, c->gp + 0xB40, flags);
    wh(c, c->gp + 0xB42, (uint16_t)baseline);
    uint32_t limit = pc + ((flags & 4) ? 0x2000 : 0xC00);
    const uint32_t fence = rp_u32(c, c->gp + 0x6E0);
    if (pc < fence && fence < limit) limit = fence;
    rp_w32(c, c->gp + 0xB54, limit);
    rp_w32(c, c->gp + 0xB4C, RECORD_BASE);
    rp_w32(c, c->gp + 0xB50, pc);
    rp_pops_analyze_records(c, RECORD_BASE);
    const uint32_t high_water = rp_u32(c, c->gp + 0xB4C);
    const uint32_t end_pc = pc + ((high_water - RECORD_BASE) >> 2);
    const uint32_t pages = (UINT32_C(2) << ((end_pc >> 16) & 31)) -
                           (UINT32_C(1) << ((pc >> 16) & 31));
    rp_w32(c, c->gp + 0xB44, 0);
    rp_w32(c, c->gp + 0x1D8, rp_u32(c, c->gp + 0x1D8) | pages);
    account_costs(c, high_water + 16);
    rp_event(c, "milestone", "POPS_record_cost_pass_complete", 0x5D5C, output);
    return output;
}
