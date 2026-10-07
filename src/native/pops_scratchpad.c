#include "pops_scratchpad.h"

/* +0x1C70/+0x1FD4/+0x2314 share the same scratchpad-store specialization.
 * Unlike the read helpers, these stores do not refund cycles to T9. */
bool rp_pops_scratchpad_store(rp_context *c)
{
    const uint32_t entry = c->run_pc;
    uint32_t target, width;
    switch (entry) {
    case 0x1DD0: case RP_STORE_SCRATCH_BYTE: target = RP_STORE_SCRATCH_BYTE; width = 1; break;
    case 0x2110: case RP_STORE_SCRATCH_HALF: target = RP_STORE_SCRATCH_HALF; width = 2; break;
    case 0x2450: case RP_STORE_SCRATCH_WORD: target = RP_STORE_SCRATCH_WORD; width = 4; break;
    default: return false;
    }
    uint32_t *r = c->run_gpr;
    const uint32_t address = r[4];
    const bool specialized = entry == target;
    bool scratch = true;
    if (specialized) {
        if ((address >> 12) != (c->gp | 0xF800)) {
            if ((address >> 23) & 63) return false;
            scratch = false;
        }
    } else if (((address >> 23) & 63) != 63 || ((address >> 10) & 0x1FFF)) {
        return false;
    }
    if (address & (width - 1)) rp_block(c, "scratchpad_store_unaligned", address);
    rp_function(c, entry, scratch ? "pops.scratchpad_store_path" : "pops.specialized_store_RAM_fallback");
    r[6] = 0x4C;
    if (!specialized) {
        const uint32_t patch = r[31] - 8;
        const bool in_ram = patch >= RP_GENERATED_RAM_BEGIN &&
            patch < rp_u32(c, RP_CORE_CACHE_ADDRESS(c, ram_code_cursor));
        const bool in_bios = patch >= RP_GENERATED_BIOS_BEGIN &&
            patch < rp_u32(c, RP_CORE_CACHE_ADDRESS(c, bios_code_cursor));
        if ((!in_ram && !in_bios) || (rp_u32(c, patch) >> 26) != 3)
            rp_block(c, "scratchpad_specialization_callsite_invalid", patch);
        r[2] = 0x0C000000 | (target >> 2);
        rp_w32(c, patch, r[2]);
        rp_event(c, "milestone", "scratchpad_store_callsite_specialized", patch, target);
    } else {
        r[2] = scratch ? address >> 12 : 0;
    }
    r[4] = scratch ? RP_PS1_SCRATCHPAD(c) | (address & 0x3FF) :
                    UINT32_C(0x09800000) | (address & 0x1FFFFF);
    for (uint32_t i = 0; i < width; ++i)
        rp_w8(c, r[4] + i, (uint8_t)(r[5] >> (i * 8)));
    c->run_pc = r[31];
    c->run_next_pc = r[31] + 4;
    return true;
}
