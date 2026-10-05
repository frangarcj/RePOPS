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

/* +0x96AC: make an enabled pending CPU interrupt due immediately. */
static uint32_t update_interrupt_deadline(rp_context *c)
{
    rp_function(c, 0x96AC, "pops.update_interrupt_deadline");
    const uint32_t status = rp_u32(c, c->gp + 0x130);
    const uint32_t cause = rp_u32(c, c->gp + 0x134);
    const uint32_t pending = status & 1 ? status & cause & 0xFF00 : 0;
    const uint32_t downcount = rp_u32(c, c->gp + 0x1B0);
    if (pending) {
        const uint32_t deadline = rp_u32(c, c->gp + 0x1AC);
        rp_w32(c, c->gp + 0x1B0, 0);
        rp_w32(c, c->gp + 0x1AC, deadline - downcount);
    }
    return pending;
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
        block = ((pc >> 23) & 63) ? rp_pops_compile_bios_block(c, pc) : rp_pops_compile_ram_block(c, pc);
        c->run_gpr[25] = rp_u32(c, c->gp + 0x1B0);
    }
    ++c->compiled_transfers;
    rp_event(c, "execution_adapter", "enter_C_generated_block", pc, block);
    return block;
}

static bool generated_address(rp_context *c, uint32_t address)
{
    return (address >= 0x09B80000 && address < rp_u32(c, c->gp + 0x1D0)) ||
           (address >= 0x09540000 && address < rp_u32(c, c->gp + 0x1CC));
}

static void native_helper(rp_context *c)
{
    uint32_t *r = c->run_gpr;
    switch (c->run_pc) {
    case 0x2878:
        rp_block(c, "RAM_code_changed_recompile_not_reconstructed", 0x4E18);
    case 0x2918: {
        rp_function(c, 0x2918, "pops.check_RAM_code_sum");
        unsigned steps = 0;
        do {
            if (++steps > 0x10000) rp_block(c, "RAM_code_sum_range_not_supported", r[4]);
            for (unsigned i = 0; i < 4; ++i) {
                r[8 + i] = rp_u32(c, r[4] + i * 4);
                r[6] -= r[8 + i];
            }
            r[4] += 16;
        } while (r[4] != r[5]);
        transfer(c, r[6] ? 0x2878 : r[31]);
        return;
    }
    case 0x1DD0:
        rp_function(c, 0x1DD0, "pops.dynamic_byte_store_RAM_path");
        r[2] = (r[4] >> 23) & 63;
        r[6] = 0x4C;
        if (r[2]) rp_block(c, "dynamic_byte_store_non_RAM_path", 0x1C70);
        r[4] = (r[4] & 0x1FFFFF) | 0x09800000;
        rp_w8(c, r[4], (uint8_t)r[5]);
        transfer(c, r[31]);
        return;
    case 0x2128: case 0x2140: case 0x2160: case 0x2180:
    case 0x1AA8: case 0x1AC8: case 0x1AE4:
    case 0x1A90: {
        const bool word_read = c->run_pc >= 0x2128 && c->run_pc <= 0x2180;
        const uint32_t entry = word_read ? 0x2128 : 0x1A90;
        rp_function(c, entry, word_read ? "pops.dynamic_word_read" : "pops.dynamic_signed_byte_read");
        const uint32_t address = r[4], region = (address >> 23) & 63;
        if (word_read && (address & 3)) rp_block(c, "dynamic_word_read_unaligned", address);
        r[6] = 0x4C;
        if (!region) {
            r[4] = (address & 0x1FFFFF) | 0x09800000;
            if (word_read) r[2] = rp_u32(c, r[4]);
            else {
                const uint8_t byte = *(uint8_t *)rp_memory(c, r[4], 1);
                r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
            }
        } else if (region == 63) {
            uint32_t specialized = 0;
            if (((address >> 10) & 0x1FFF) == 0) {
                specialized = word_read ? 0x2140 : 0x1AA8;
                r[4] = (address & 0x3FF) | 0x13000;
                if (word_read) r[2] = rp_u32(c, r[4]);
                else {
                    const uint8_t byte = *(uint8_t *)rp_memory(c, r[4], 1);
                    r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
                }
                r[25] += 4;
            } else if ((address >> 19) == 0x17F8) {
                specialized = word_read ? 0x2160 : 0x1AC8;
                const uint32_t offset = 0x53C20 + (address & 0x7FFFF);
                if (word_read) {
                    r[2] = rp_module_u32(c, offset);
                    r[25] -= 3;
                } else {
                    const uint8_t byte = *(uint8_t *)rp_module_memory(c, offset, 1);
                    r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
                }
            } else {
                rp_block(c, "read_IO_specialization_not_reconstructed", address);
            }
            if (c->run_pc == entry) {
                const uint32_t patch = r[31] - 8;
                if (!generated_address(c, patch))
                    rp_block(c, "memory_specialization_patch_outside_cache", patch);
                rp_w32(c, patch, (UINT32_C(0x30000000) + specialized) >> 2);
                rp_event(c, "milestone", "read_callsite_specialized", patch, specialized);
            }
        } else {
            /* +0x1C68 -> +0x8ADC: no call-site specialization in this path. */
            r[5] = word_read ? 2 : 0;
            rp_w32(c, c->gp + 0x1B0, r[25]);
            r[2] = rp_pops_constant_read(c, r[4], r[5]);
            r[25] = rp_u32(c, c->gp + 0x1B0);
        }
        transfer(c, r[31]);
        return;
    }
    case 0x2648:
        rp_w32(c, c->gp + 0x1B0, r[25]);
        if ((int32_t)r[25] <= 0) {
            r[2] = rp_pops_dispatch_events(c);
            r[25] = rp_u32(c, c->gp + 0x1B0);
        }
        r[4] = rp_u32(c, c->gp + 0x1A0);
        transfer(c, lookup_block(c, r[4]));
        return;
    case 0x7F00:
        rp_pops_spu_write_register(c, r[4], r[5], r[6]);
        transfer(c, r[31]);
        return;
    case 0x2450:
        rp_function(c, 0x2450, "pops.dynamic_word_store_RAM_path");
        r[2] = (r[4] >> 23) & 63;
        r[6] = 0x4C;
        if (r[2]) rp_block(c, "dynamic_word_store_non_RAM_path", 0x2314);
        r[4] = (r[4] & 0x1FFFFF) | 0x09800000;
        if (r[4] & 3) rp_block(c, "dynamic_word_store_unaligned", r[4]);
        rp_w32(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x96AC:
        r[2] = update_interrupt_deadline(c);
        transfer(c, r[31]);
        return;
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
        if (!generated_address(c, patch))
            rp_block(c, "link_patch_outside_generated_cache", patch);
        /* Original SWL changes only 24 target bits: PRX and cache share the
         * remaining bits on PSP. Our base-zero helper addresses do not, so
         * the rehost must replace the full JAL target, retaining its opcode.
         */
        rp_w32(c, patch, (rp_u32(c, patch) & 0xFC000000) | ((target >> 2) & 0x3FFFFFF));
        r[4] = target << 6;
        rp_event(c, "milestone", "BIOS_block_executed_next_PC_reached", target_pc, target);
        transfer(c, target);
        return;
    }
    case 0x1A68:
        rp_w32(c, c->gp + 0x1A0, r[2]);
        rp_w32(c, c->gp + 0x1B4, r[31]);
        rp_w32(c, c->gp + 0x1B0, r[25]);
        r[2] = rp_pops_dispatch_events(c);
        r[25] = rp_u32(c, c->gp + 0x1B0);
        transfer(c, r[2]);
        return;
    case 0x1A80:
        rp_w32(c, c->gp + 0x1B0, r[25]);
        r[2] = rp_pops_dispatch_events(c);
        r[25] = rp_u32(c, c->gp + 0x1B0);
        transfer(c, lookup_block(c, rp_u32(c, c->gp + 0x1A0)));
        return;
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
    rp_unicorn_open(c);
    transfer(c, lookup_block(c, rp_u32(c, c->gp + 0x1A0)));
    for (unsigned steps = 0; steps < 100000; ++steps) {
        if (generated_address(c, c->run_pc))
            rp_unicorn_run(c);
        else
            native_helper(c);
    }
    rp_block(c, "generated_execution_diagnostic_budget", c->run_pc);
}
