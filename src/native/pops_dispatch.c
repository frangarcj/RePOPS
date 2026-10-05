#include "runtime.h"
#include "pops_emit.h"
#include <string.h>

/* +0x7E60. CACHE/SYNC are host-coherent data operations in this diagnostic.
 * The fill uses the reset's actual R403 row, not an invented success result.
 */
void rp_pops_invalidate_ram_code(rp_context *c)
{
    rp_function(c, 0x7E60, "pops.invalidate_RAM_code_pages");
    if (!c->vfpu_zero_ready) rp_block(c, "cache_fill_vector_not_initialized", 0x7E60);
    uint32_t pages = rp_u32(c, c->gp + 0x1D8), address = 0x09C00000;
    while (pages) {
        if (pages & 1) {
            uint8_t *destination = rp_memory(c, address, 0x10000);
            for (unsigned i = 0; i < 0x10000; i += 16)
                memcpy(destination + i, c->vfpu_reset_rows[3], 16);
        }
        pages >>= 1;
        rp_w32(c, c->gp + 0x1D8, pages);
        address += 0x10000;
    }
    rp_w32(c, c->gp + 0x1CC, 0x09540000);
    rp_w32(c, c->gp + 0x1D8, 0);
    rp_w32(c, c->gp + 0x1D4, 0);
    rp_w32(c, c->gp + 0x3CF4, 0);
    rp_w32(c, c->gp + 0x3CF0, 0);
}

/* +0x89A0. Default/unmapped writes and the PS1 cache-control register. */
void rp_pops_default_write(rp_context *c, uint32_t address, uint32_t value, uint32_t kind)
{
    rp_function(c, 0x89A0, "pops.default_memory_write");
    if (address == UINT32_C(0xFFFE0130)) {
        rp_w32(c, c->gp + 0x1E0, value);
        if (value == 0x804) {
            rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - 0x20);
            rp_pops_invalidate_ram_code(c);
        }
    } else if (address != 0x1F802040 && address != 0x1F802041 &&
               address != 0x1F802030 && address != 0x1F802070 &&
               (address & UINT32_C(0x1FFFFFFF)) + UINT32_C(0xE0400000) < 0x80000) {
        rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - ((1u << (kind & 3)) + 15));
    }
}

static void transfer(rp_context *c, uint32_t target)
{
    c->run_pc = target;
    c->run_next_pc = target + 4;
}

/* +0x2650 cache address computation. Cache misses use the reconstructed
 * +0x58C0; unsupported records stop there, never in a firmware interpreter.
 */
static uint32_t lookup_block(rp_context *c, uint32_t pc)
{
    const uint32_t table = 0x09C00000 | (pc & 0x1FFFFF) | (((pc >> 23) & 1) << 21);
    if (pc & 3) rp_block(c, "unaligned_PS1_dispatch_target", pc);
    uint32_t block = rp_u32(c, table);
    if (!block) {
        rp_w32(c, c->gp + 0x1B0, c->run_gpr[25]);
        block = rp_pops_compile_bios_block(c, pc);
        c->run_gpr[25] = rp_u32(c, c->gp + 0x1B0);
    }
    ++c->compiled_transfers;
    rp_event(c, "execution_adapter", "enter_C_generated_block", pc, block);
    return block;
}

static void native_helper(rp_context *c)
{
    uint32_t *r = c->run_gpr;
    switch (c->run_pc) {
    case 0x89A0:
        rp_pops_default_write(c, r[4], r[5], r[6]);
        transfer(c, r[31]);
        return;
    case 0x2888: {
        /* Original stores RA, resolves the block, then SWL-patches the JAL
         * preceding RA. There is no return to the old block: this is a tail
         * transfer into the newly resolved guest block.
         */
        rp_function(c, 0x2888, "pops.link_and_dispatch_compiled_block");
        rp_w32(c, c->gp + 0xFC, r[31]);
        const uint32_t target_pc = r[4];
        rp_event(c, "milestone", "BIOS_generated_block_exit", target_pc, (uint32_t)c->generated_instructions);
        const uint32_t target = lookup_block(c, target_pc);
        r[2] = target;
        r[31] = rp_u32(c, c->gp + 0xFC);
        const uint32_t patch = r[31] - 8;
        if (patch < 0x09B80000 || patch >= rp_u32(c, c->gp + 0x1D0))
            rp_block(c, "link_patch_outside_generated_cache", patch);
        rp_w32(c, patch, (rp_u32(c, patch) & 0xFF000000) | ((target >> 2) & 0xFFFFFF));
        r[4] = target << 6;
        rp_event(c, "milestone", "BIOS_block_executed_next_PC_reached", target_pc, target);
        transfer(c, target);
        return;
    }
    case 0x1A80:
        rp_w32(c, c->gp + 0x1B0, r[25]);
        rp_block(c, "guest_event_dispatch_not_reconstructed", 0x953C);
    default:
        rp_event(c, "native_helper_boundary", "generated_call_into_POPS", c->run_pc, r[4]);
        rp_block(c, "native_core_helper_not_reconstructed", c->run_pc);
    }
}

void rp_pops_run_core(rp_context *c)
{
    rp_function(c, 0x1A00, "pops.core_dispatch_entry");
    memset(c->run_gpr, 0, sizeof(c->run_gpr));
    memset(c->run_fpr, 0, sizeof(c->run_fpr));
    c->run_gpr[28] = c->gp;
    c->run_gpr[29] = 0x09800000;
    c->run_gpr[25] = rp_u32(c, c->gp + 0x1B0);
    rp_event(c, "execution_adapter", "generated_code_only_not_original_PRX", 0x1A00, 0);
    transfer(c, lookup_block(c, rp_u32(c, c->gp + 0x1A0)));
    for (unsigned steps = 0; steps < 100000; ++steps) {
        if (c->run_pc >= 0x09B80000 && c->run_pc < rp_u32(c, c->gp + 0x1D0))
            rp_generated_step(c);
        else
            native_helper(c);
    }
    rp_block(c, "generated_execution_diagnostic_budget", c->run_pc);
}
