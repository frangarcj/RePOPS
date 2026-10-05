#include "runtime.h"
#include "pops_ir.h"
#include "pops_emit.h"
#include <string.h>

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
                    if (h(c, q + 4) == RP_CAT_MEMORY) break;
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
                    if ((next_op == RP_OP_COP2 && b(c, q + 2) != 0) || next_op == RP_OP_SWC2) break;
                    remaining -= b(c, q + 15);
                    if (remaining < 2) { remaining = 1; break; }
                    if (h(c, q) & 1) { remaining >>= 1; break; }
                }
            } else if (opcode - UINT32_C(0x58) < 4) {
                for (uint32_t q = p + 16; q < end; q += 16) {
                    const uint32_t next_op = b(c, q + 3);
                    if (next_op == RP_OP_MFHI || next_op == RP_OP_MFLO) break;
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

static uint32_t allocate_source(rp_context *c, uint32_t out, uint32_t reg, uint32_t protect)
{
    if (!reg) return out;
    return (rp_emit_allocate(c, out, reg, protect, 2) >> 5) << 2;
}

/* +0x5E78..+0x64F7, BIOS record-walk path. Entry setup has already run.
 * Output addresses replace category/cost at record+4 for join entries, so
 * the current category must be saved before those in-place writes.
 * The final linking/cache-table pass is deliberately a separate boundary.
 */
uint32_t rp_pops_emit_block_records(rp_context *c, uint32_t out)
{
    if (!(h(c, c->gp + 0xB40) & 0x8000))
        rp_block(c, "RAM_record_controller_not_reconstructed", 0x58C0);
    const uint32_t end = rp_u32(c, c->gp + 0xB4C) + 16;
    for (uint32_t record = RECORD_BASE; record < end;) {
        const rp_pops_category category = (rp_pops_category)h(c, record + 4);
        if (category == RP_CAT_EMPTY) { record += 16; continue; }
        uint16_t flags = h(c, record);
        bool record_entry = false;
        if (flags & 8) {
            const uint32_t cost = h(c, record + 6) + rp_u32(c, c->gp + 0xB44);
            out = rp_emit_debit(c, (int32_t)cost, out);
            rp_w32(c, c->gp + 0xB58, 0x80000000);
            rp_w32(c, c->gp + 0xB44, 0);
            rp_w32(c, c->gp + 0xB5C, 0);
            out = rp_emit_flush_registers(c, out, 11);
            rp_w32(c, c->gp + 0x740, out);
            record_entry = true;
        } else if (rp_u32(c, c->gp + 0xB58) == 0x80000000) {
            uint32_t busy = rp_u32(c, c->gp + 0x744) |
                b(c, c->gp + 0x750) | b(c, c->gp + 0x752);
            for (unsigned i = 0; i < 12; ++i) busy |= b(c, c->gp + 0x760 + i);
            if (!busy) {
                flags |= 8; wh(c, record, flags);
                record_entry = true;
            }
        }
        if (record_entry) rp_w32(c, record + 4, out);

        if ((flags & 0x8000) && !(flags & 0x4000) &&
                h(c, record + 4) != RP_CAT_MEMORY) {
            uint32_t next_rt = b(c, record + 29), next_rs = b(c, record + 28);
            if (!(h(c, record + 20) == RP_CAT_ALU && (!next_rt || !next_rs))) {
                const uint32_t dest = b(c, record + 2);
                if (next_rt == dest) next_rt = 0;
                if (next_rs == dest) next_rs = 0;
                if (next_rt || next_rs) {
                    const uint32_t rt = b(c, record + 13), rs = b(c, record + 12);
                    out = allocate_source(c, out, rt, (1u << (rs & 31)) | (1u << (next_rt & 31)));
                    out = allocate_source(c, out, rs, (1u << (rt & 31)) | (1u << (next_rt & 31)));
                    const uint32_t protect = (1u << (rt & 31)) | (1u << (rs & 31));
                    out = allocate_source(c, out, next_rt, protect);
                    out = allocate_source(c, out, next_rs, protect);
                }
            }
        }
        if (flags & 0x1000)
            rp_block(c, "compiler_special_PC_hook_not_reconstructed", 0x6088);

        const uint16_t next_flags = h(c, record + 16);
        if (!(next_flags & 1)) {
            const uint8_t dest = b(c, record + 2);
            if ((flags & 0x40) && (flags & 0x8000) &&
                    (b(c, record + 28) == dest || b(c, record + 29) == dest)) {
                if (!(h(c, record + 32) & 1)) {
                    if (b(c, record + 18) != dest) {
                        out = rp_emit_record(c, (rp_pops_category)h(c, record + 20), record + 16, out, 0);
                        wh(c, record + 20, RP_CAT_EMPTY);
                    }
                } else if (b(c, record + 19) >= RP_OP_BGEZ) {
                    rp_block(c, "load_delay_branch_capture_not_reconstructed", 0x5064);
                }
            }
            out = rp_emit_record(c, category, record, out, 0);
            record += 16;
            continue;
        }

        /* +0x62D4..0x63FC: capture a conditional source before an ordinary
         * delay slot overwrites it. Nested branches/load hazards stay explicit.
         */
        if ((category != RP_CAT_JUMP_DIRECT && category != RP_CAT_BRANCH) || (next_flags & 0x40) ||
                (h(c, record + 32) & 1)) {
            const uint32_t pc = rp_u32(c, c->gp + 0xB50) + ((record - RECORD_BASE) >> 2);
            rp_event(c, "compiler_boundary", "delay_record_category", pc, category);
            rp_event(c, "compiler_boundary", "delay_record_flags_and_next", flags, next_flags);
            rp_event(c, "compiler_boundary", "delay_record_sources_and_opcode", rp_u32(c, record + 12), b(c, record + 3));
            rp_block(c, "complex_delay_slot_controller_not_reconstructed", 0x61B0);
        }
        if (category == RP_CAT_BRANCH && !(flags & 0x20)) {
            const uint32_t rs = b(c, record + 12), rt = b(c, record + 13);
            const uint32_t mask = rp_u32(c, c->gp + 0xB58);
            if ((mask & (0x80000000u >> rs)) && (mask & (0x80000000u >> rt)))
                rp_block(c, "constant_branch_controller_not_reconstructed", 0x6224);
            const uint32_t dest = b(c, record + 18), link = b(c, record + 2);
            if ((dest && (dest == rs || dest == rt)) || (link && (link == rs || link == rt))) {
                out = rp_emit_capture_branch(c, record, out);
                flags = h(c, record) | 0x20;
                wh(c, record, flags);
            }
        }
        const uint32_t link = b(c, record + 2);
        if (link) {
            const uint32_t pc = rp_u32(c, c->gp + 0xB50) + ((record - RECORD_BASE) >> 2);
            out = rp_emit_known_value(c, link, pc + 8, out, 0);
        }
        out = rp_emit_record(c, (rp_pops_category)h(c, record + 20), record + 16, out, 0);
        out = rp_emit_record(c, category, record, out, h(c, record + 22));
        record += (h(c, record + 16) & 8) ? 16 : 32;
    }
    out = rp_emit_flush_registers(c, out, 11);
    rp_w32(c, c->gp + 0x740, out);
    rp_event(c, "milestone", "POPS_BIOS_record_walk_complete_before_linking", 0x64F8, out);
    return out;
}

/* +0x64F8..+0x6767 for the BIOS profile. Cache-maintenance instructions have
 * no host analogue here: the published addresses still refer to Allegrex data.
 */
uint32_t rp_pops_publish_bios_block(rp_context *c, uint32_t pc, uint32_t entry, uint32_t out)
{
    const uint16_t mode = h(c, c->gp + 0xB40);
    if (!(mode & 0x8000) || (mode & 2))
        rp_block(c, "non_BIOS_link_profile_not_reconstructed", 0x64F8);
    if (!c->vfpu_zero_ready)
        rp_block(c, "compiler_vector_fill_source_not_initialized", 0x665C);
    const uint32_t end = rp_u32(c, c->gp + 0xB4C) + 16;
    const uint32_t base_pc = rp_u32(c, c->gp + 0xB50);
    for (uint32_t record = RECORD_BASE; record < end; record += 16) {
        const uint16_t flags = h(c, record);
        if (flags & 2) {
            const uint32_t patch = rp_u32(c, record + 12);
            const uint32_t target_record = RECORD_BASE + (rp_u32(c, record + 8) - base_pc) * 4;
            const uint32_t target = rp_u32(c, target_record + 4);
            uint32_t instruction = rp_u32(c, patch);
            if (instruction >> 27 == 1) {
                instruction = (instruction & 0xFC000000) | ((target >> 2) & 0x3FFFFFF);
                if (target == patch) instruction = 0;
            } else {
                instruction = (instruction & 0xFFFF0000) | (((target - patch - 4) >> 2) & 0xFFFF);
            }
            rp_w32(c, patch, instruction);
        }
        if ((flags & 0x18) == 8) {
            const uint32_t target_pc = base_pc + ((record - RECORD_BASE) >> 2);
            const uint32_t table = ((target_pc & 0x1FFFFFFF) >> 23) ? 0x09E00000 : 0x09C00000;
            rp_w32(c, table + (target_pc & 0x1FFFFC), rp_u32(c, record + 4));
        }
        memcpy(rp_memory(c, record, 16), c->vfpu_reset_rows[3], 16);
    }
    memcpy(rp_memory(c, end, 16), c->vfpu_reset_rows[3], 16);
    if (h(c, c->gp + 0xB42))
        rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - ((end - RECORD_BASE) >> 4));
    rp_w32(c, c->gp + 0x1D0, out);
    const uint32_t table = ((pc & 0x1FFFFFFF) >> 23) ? 0x09E00000 : 0x09C00000;
    rp_w32(c, table + (pc & 0x1FFFFC), entry);
    if (pc == 0x80000080) rp_w32(c, c->gp + 0x1D4, entry);
    rp_event(c, "milestone", "POPS_BIOS_block_published_not_host_executable", entry, out - entry);
    return entry;
}

uint32_t rp_pops_compile_bios_block(rp_context *c, uint32_t pc)
{
    if ((pc & 0x1FFFFFFF) < 0x1FC00000 || (pc & 0x1FFFFFFF) >= 0x1FC80000)
        rp_block(c, "non_BIOS_compiler_controller_not_reconstructed", 0x58C0);
    const uint32_t entry = rp_pops_prepare_compile(c, pc);
    rp_emit_init_registers(c, entry);
    const uint32_t out = rp_pops_emit_block_records(c, entry);
    return rp_pops_publish_bios_block(c, pc, entry, out);
}
