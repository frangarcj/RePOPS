#include "runtime.h"
#include "pops_state.h"
#include "pops_cdrom.h"
#include "pops_dma.h"
#include "pops_gpu.h"
#include "pops_gte.h"
#include "pops_timer.h"
#include "pops_emit.h"
#include <errno.h>
#include <stdlib.h>
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
            rp_core_set_downcount(c, rp_core_downcount(c) - 0x20);
            rp_pops_invalidate_ram_code(c);
        }
    } else if (address != 0x1F802040 && address != 0x1F802041 &&
               address != 0x1F802030 && address != 0x1F802070 &&
               (address & UINT32_C(0x1FFFFFFF)) + UINT32_C(0xE0400000) < 0x80000) {
        rp_core_set_downcount(c, rp_core_downcount(c) - ((1u << (kind & 3)) + 15));
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
    const uint32_t downcount = rp_core_downcount(c);
    if (pending) {
        const uint32_t deadline = rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline));
        rp_core_set_downcount(c, 0);
        rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), deadline - downcount);
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
        rp_core_set_downcount(c, c->run_gpr[25]);
        block = ((pc >> 23) & 63) ? rp_pops_compile_bios_block(c, pc) : rp_pops_compile_ram_block(c, pc);
        c->run_gpr[25] = rp_core_downcount(c);
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

static void specialize_generated_call(rp_context *c, uint32_t return_address,
                                      uint32_t target, const char *name)
{
    const uint32_t patch = return_address - 8;
    if (!generated_address(c, patch))
        rp_block(c, "memory_specialization_patch_outside_cache", patch);
    const uint32_t word = rp_u32(c, patch);
    if ((word >> 26) != 3)
        rp_block(c, "memory_specialization_not_a_JAL", patch);
    rp_w32(c, patch, 0x0C000000 | ((target >> 2) & 0x03FFFFFF));
    rp_event(c, "milestone", name, patch, target);
}

static void native_helper(rp_context *c)
{
    uint32_t *r = c->run_gpr;
    switch (c->run_pc) {
    case RP_GTE_NCDS_HELPER:
        rp_pops_gte_ncds(c);
        transfer(c, r[31]);
        return;
    case RP_GTE_AVSZ3_HELPER: case RP_GTE_AVSZ4_HELPER:
        rp_pops_gte_avsz(c, c->run_pc == RP_GTE_AVSZ4_HELPER);
        transfer(c, r[31]);
        return;
    case RP_GTE_NCLIP_HELPER:
        rp_pops_gte_nclip(c);
        transfer(c, r[31]);
        return;
    case RP_GTE_RTPT_FLAGS_HELPER: case RP_GTE_RTPT_NO_FLAGS_HELPER:
        rp_pops_gte_rtpt(c, c->run_pc == RP_GTE_RTPT_FLAGS_HELPER);
        transfer(c, r[31]);
        return;
    case 0x127D8:
        rp_pops_gpu_write(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x12FBC:
        r[2] = rp_pops_gpu_read(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0xD088:
        r[2] = rp_pops_cd_read(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0xD1B0:
        rp_pops_cd_write(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x85F4:
        rp_pops_me_service_due(c);
        r[2] = rp_pops_spu_read_register(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x9BE0:
        r[2] = rp_pops_timer_read(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x9158:
        r[2] = rp_pops_dma_read(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x9C60:
        rp_pops_timer_write(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
    case 0x91BC:
        rp_pops_dma_control_write(c, r[4], r[5], r[6]);
        transfer(c, r[31]);
        return;
    case 0x92A4:
        rp_pops_dma_channel_write(c, r[4], r[5], r[6]);
        transfer(c, r[31]);
        return;
    case 0x94C4:
        rp_pops_prepare_exception(c, r[4]);
        r[2] = rp_u32(c, c->gp + 0x1B4);
        transfer(c, r[31]);
        return;
    case 0x9850:
        r[2] = rp_pops_irq_read(c, r[4]);
        transfer(c, r[31]);
        return;
    case 0x98C4:
        rp_pops_irq_write(c, r[4], r[5]);
        transfer(c, r[31]);
        return;
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
        rp_function(c, 0x1DD0, "pops.dynamic_byte_store");
        r[2] = (r[4] >> 23) & 63;
        r[6] = 0x4C;
        if (r[2]) {
            const uint32_t handler = rp_device_handler(c, r[4], true);
            if (handler != 0xD1B0) rp_block(c, "dynamic_byte_store_non_RAM_path", r[4]);
            rp_core_set_downcount(c, r[25]);
            rp_pops_cd_write(c, r[4], r[5]);
            r[25] = rp_core_downcount(c);
            transfer(c, r[31]);
            return;
        }
        specialize_generated_call(c, r[31], RP_FAST_RAM_SB,
                                  "byte_store_RAM_callsite_specialized");
        r[4] = (r[4] & 0x1FFFFF) | 0x09800000;
        rp_w8(c, r[4], (uint8_t)r[5]);
        transfer(c, r[31]);
        return;
    case 0x2110: {
        rp_function(c, 0x2110, "pops.dynamic_halfword_store");
        const uint32_t address = r[4], region = (address >> 23) & 63;
        r[6] = 0x4C;
        if (!region) {
            specialize_generated_call(c, r[31], RP_FAST_RAM_SH,
                                      "halfword_store_RAM_callsite_specialized");
            r[4] = (address & 0x1FFFFF) | 0x09800000;
            if (r[4] & 1) rp_block(c, "dynamic_halfword_store_unaligned", r[4]);
            rp_w8(c, r[4], (uint8_t)r[5]);
            rp_w8(c, r[4] + 1, (uint8_t)(r[5] >> 8));
        } else {
            uint32_t index = (address + UINT32_C(0xE07FF000)) >> 3;
            if (index > 0x1FF) index = 0x1FF;
            const uint32_t handler = rp_u32(c, c->gp + 0x1004 + index * 8);
            if (handler != 0x9C60 && handler != 0x98C4 && handler != 0x91BC && handler != 0x7F00)
                rp_block(c, "dynamic_halfword_store_not_reconstructed", address);
            rp_core_set_downcount(c, r[25]);
            if (handler == 0x98C4) rp_pops_irq_write(c, address, r[5]);
            else if (handler == 0x91BC) rp_pops_dma_control_write(c, address, r[5], 1);
            else if (handler == 0x7F00) rp_pops_spu_write_register(c, address, r[5], 1);
            else rp_pops_timer_write(c, address, r[5]);
            r[25] = rp_core_downcount(c);
        }
        transfer(c, r[31]);
        return;
    }
    case 0x2128: case 0x2140: case 0x2160: case 0x2180:
    case 0x267C: case 0x2694: case 0x26B4: case 0x26D4:
    case 0x1DE8: case 0x1E00: case 0x1E20: case 0x1E40:
    case 0x1AA8: case 0x1AC8: case 0x1AE4:
    case 0x1A90: {
        const bool word_read = c->run_pc >= 0x2128 && c->run_pc <= 0x2180;
        const bool signed_half_read = c->run_pc >= 0x1DE8 && c->run_pc <= 0x1E40;
        const bool half_read = signed_half_read || (c->run_pc >= 0x267C && c->run_pc <= 0x26D4);
        const uint32_t width = word_read ? 2 : signed_half_read ? 1 : half_read ? 5 : 0;
        const uint32_t entry = word_read ? 0x2128 : signed_half_read ? 0x1DE8 : half_read ? 0x267C : 0x1A90;
        rp_function(c, entry, word_read ? "pops.dynamic_word_read" :
                    signed_half_read ? "pops.dynamic_signed_halfword_read" :
                    half_read ? "pops.dynamic_unsigned_halfword_read" : "pops.dynamic_signed_byte_read");
        const uint32_t address = r[4], region = (address >> 23) & 63;
        if (word_read && (address & 3)) rp_block(c, "dynamic_word_read_unaligned", address);
        if (half_read && (address & 1)) rp_block(c, "dynamic_halfword_read_unaligned", address);
        r[6] = 0x4C;
        uint32_t specialized = 0;
        if (!region) {
            if (word_read)
                specialized = RP_FAST_RAM_LW;
            else if (half_read)
                specialized = signed_half_read ? RP_FAST_RAM_LH : RP_FAST_RAM_LHU;
            r[4] = (address & 0x1FFFFF) | 0x09800000;
            if (word_read) r[2] = rp_u32(c, r[4]);
            else if (half_read) {
                const uint8_t *p = rp_memory(c, r[4], 2);
                r[2] = rp_halfword_value(p[0] | (uint32_t)p[1] << 8, signed_half_read);
            }
            else {
                const uint8_t byte = *(uint8_t *)rp_memory(c, r[4], 1);
                r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
            }
        } else if (region == 63) {
            if (((address >> 10) & 0x1FFF) == 0) {
                specialized = word_read ? 0x2140 : signed_half_read ? 0x1E00 : half_read ? 0x2694 : 0x1AA8;
                r[4] = (address & 0x3FF) | 0x13000;
                if (word_read) r[2] = rp_u32(c, r[4]);
                else if (half_read) {
                    const uint8_t *p = rp_memory(c, r[4], 2);
                    r[2] = rp_halfword_value(p[0] | (uint32_t)p[1] << 8, signed_half_read);
                }
                else {
                    const uint8_t byte = *(uint8_t *)rp_memory(c, r[4], 1);
                    r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
                }
                r[25] += 4;
            } else if ((address >> 19) == 0x17F8) {
                specialized = word_read ? 0x2160 : signed_half_read ? 0x1E20 : half_read ? 0x26B4 : 0x1AC8;
                const uint32_t offset = 0x53C20 + (address & 0x7FFFF);
                if (word_read) {
                    r[2] = rp_module_u32(c, offset);
                    r[25] -= 3;
                } else if (half_read) {
                    const uint8_t *p = rp_module_memory(c, offset, 2);
                    r[2] = rp_halfword_value(p[0] | (uint32_t)p[1] << 8, signed_half_read);
                    r[25] -= 1;
                } else {
                    const uint8_t byte = *(uint8_t *)rp_module_memory(c, offset, 1);
                    r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
                }
            } else {
                uint32_t index = (address + UINT32_C(0xE07FF000)) >> 3;
                if (index > 0x1FF) index = 0x1FF;
                const uint32_t handler = rp_u32(c, c->gp + 0x1000 + index * 8);
                if ((word_read || half_read) && handler == 0x9850) {
                    rp_core_set_downcount(c, r[25]);
                    r[2] = rp_pops_irq_read(c, address);
                    r[25] = rp_core_downcount(c);
                } else if (handler == 0xD088) {
                    r[2] = rp_pops_cd_read(c, address, width);
                } else if (handler == 0x12FBC) {
                    rp_core_set_downcount(c, r[25]);
                    r[2] = rp_pops_gpu_read(c, address, width);
                    r[25] = rp_core_downcount(c);
                } else if (handler == 0x9BE0) {
                    rp_core_set_downcount(c, r[25]);
                    r[2] = rp_pops_timer_read(c, address, width);
                    r[25] = rp_core_downcount(c);
                } else if (handler == 0x9158) {
                    rp_core_set_downcount(c, r[25]);
                    r[2] = rp_pops_dma_read(c, address, width);
                    r[25] = rp_core_downcount(c);
                } else if (handler == 0x85F4) {
                    rp_core_set_downcount(c, r[25]);
                    rp_pops_me_service_due(c);
                    r[2] = rp_pops_spu_read_register(c, address, width);
                    r[25] = rp_core_downcount(c);
                } else if (handler == 0x8A54) {
                    rp_function(c, 0x8A54, "pops.read_shadow_register");
                    const uint32_t shadow = c->gp + (address & 0xFFF) + 0x2000;
                    if (word_read) r[2] = rp_u32(c, shadow);
                    else if (half_read) {
                        const uint8_t *p = rp_memory(c, shadow, 2);
                        r[2] = rp_halfword_value(p[0] | (uint32_t)p[1] << 8, signed_half_read);
                    } else {
                        const uint8_t byte = *(uint8_t *)rp_memory(c, shadow, 1);
                        r[2] = byte < 128 ? byte : (uint32_t)((int32_t)byte - 256);
                    }
                } else {
                    rp_block(c, "read_IO_specialization_not_reconstructed", address);
                }
            }
        } else {
            /* +0x1C68 -> +0x8ADC: no call-site specialization in this path. */
            r[5] = width;
            rp_core_set_downcount(c, r[25]);
            r[2] = rp_pops_constant_read(c, r[4], r[5]);
            r[25] = rp_core_downcount(c);
        }
        if (specialized && c->run_pc == entry) {
            const uint32_t patch = r[31] - 8;
            if (!generated_address(c, patch))
                rp_block(c, "memory_specialization_patch_outside_cache", patch);
            rp_w32(c, patch, (UINT32_C(0x30000000) + specialized) >> 2);
            rp_event(c, "milestone", "read_callsite_specialized", patch, specialized);
        }
        transfer(c, r[31]);
        return;
    }
    case 0x2648:
        rp_core_set_downcount(c, r[25]);
        if ((int32_t)r[25] <= 0) {
            r[2] = rp_pops_dispatch_events(c);
            r[25] = rp_core_downcount(c);
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
        if (!r[2]) {
            specialize_generated_call(c, r[31], RP_FAST_RAM_SW,
                                      "word_store_RAM_callsite_specialized");
            r[4] = (r[4] & 0x1FFFFF) | 0x09800000;
            if (r[4] & 3) rp_block(c, "dynamic_word_store_unaligned", r[4]);
            rp_w32(c, r[4], r[5]);
        } else {
            const uint32_t handler = rp_device_handler(c, r[4], true);
            if (handler != 0x98C4 && handler != 0x91BC && handler != 0x9C60 &&
                    handler != 0x7F00 && handler != 0x8AA4 && handler != 0x92A4 && handler != 0x127D8)
                rp_block(c, "dynamic_word_store_non_RAM_path", r[4]);
            rp_core_set_downcount(c, r[25]);
            if (handler == 0x127D8) rp_pops_gpu_write(c, r[4], r[5]);
            else if (handler == 0x92A4) rp_pops_dma_channel_write(c, r[4], r[5], 2);
            else if (handler == 0x8AA4) rp_pops_shadow_write(c, r[4], r[5], 2);
            else if (handler == 0x91BC) rp_pops_dma_control_write(c, r[4], r[5], 2);
            else if (handler == 0x9C60) rp_pops_timer_write(c, r[4], r[5]);
            else if (handler == 0x7F00) rp_pops_spu_write_register(c, r[4], r[5], 2);
            else rp_pops_irq_write(c, r[4], r[5]);
            r[25] = rp_core_downcount(c);
        }
        transfer(c, r[31]);
        return;
    case 0x2468: {
        rp_function(c, 0x2468, "pops.dynamic_unsigned_byte_read");
        const uint32_t address = r[4];
        const uint32_t physical = address & 0x1FFFFFFF;
        const uint32_t region = (address >> 23) & 63;
        r[6] = 0x4C;
        if (!region) {
            specialize_generated_call(c, r[31], RP_FAST_RAM_LBU,
                                      "byte_read_RAM_callsite_specialized");
            r[4] = (address & 0x1FFFFF) | 0x09800000;
            r[2] = *(uint8_t *)rp_memory(c, r[4], 1);
        } else if (physical >= 0x1FC00000 && physical < 0x1FC80000) {
            specialize_generated_call(c, r[31], RP_FAST_BIOS_LBU,
                                      "byte_read_BIOS_callsite_specialized");
            r[2] = *(uint8_t *)rp_module_memory(c, 0x53C20 + physical - 0x1FC00000, 1);
        } else if (region == 63 && ((address >> 10) & 0x1FFF) == 0) {
            r[4] = (address & 0x3FF) | 0x13000;
            r[2] = *(uint8_t *)rp_memory(c, r[4], 1);
            r[25] += 4;
        } else {
            r[5] = 4;
            rp_core_set_downcount(c, r[25]);
            if (rp_device_handler(c, address, false) == 0xD088)
                r[2] = rp_pops_cd_read(c, address, 4);
            else
                r[2] = rp_pops_constant_read(c, address, 4);
            r[25] = rp_core_downcount(c);
        }
        transfer(c, r[31]);
        return;
    }
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
        rp_core_set_downcount(c, r[25]);
        r[2] = rp_pops_dispatch_events(c);
        r[25] = rp_core_downcount(c);
        transfer(c, r[2]);
        return;
    case 0x1A80:
        rp_core_set_downcount(c, r[25]);
        r[2] = rp_pops_dispatch_events(c);
        r[25] = rp_core_downcount(c);
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
    /* Host diagnostic limit only: never change the guest cycle downcount or
     * synthesize a ready event to extend the observed execution window. */
    uint32_t limit = 500000;
    const char *setting = getenv("REPOPS_RUN_STEPS");
    if (setting && *setting) {
        char *end;
        errno = 0;
        const unsigned long value = strtoul(setting, &end, 10);
        if (errno || *end || !value || value > 10000000)
            rp_block(c, "invalid_host_execution_step_limit", 0x1A00);
        limit = (uint32_t)value;
    }
    rp_event(c, "execution_adapter", "host_dispatch_step_limit", 0, limit);
    memset(c->run_gpr, 0, sizeof(c->run_gpr));
    memset(c->run_fpr, 0, sizeof(c->run_fpr));
    c->run_gpr[28] = c->gp;
    c->run_gpr[29] = 0x09800000;
    c->run_gpr[25] = rp_core_downcount(c);
    rp_unicorn_open(c);
    transfer(c, lookup_block(c, rp_u32(c, c->gp + 0x1A0)));
    for (uint32_t steps = 0; steps < limit; ++steps) {
        if (generated_address(c, c->run_pc))
            rp_unicorn_run(c);
        else
            native_helper(c);
    }
    rp_event(c, "diagnostic_state", "guest_cycles_at_host_limit", rp_core_guest_cycles(c), limit);
    rp_event(c, "diagnostic_state", "display_at_host_limit",
             rp_u32(c, RP_GPU_ADDRESS(c, frame_counter)), rp_u32(c, RP_GPU_ADDRESS(c, status)));
    rp_block(c, "generated_execution_diagnostic_budget", c->run_pc);
}
