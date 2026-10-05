/* Execution support, not a reconstructed POPS function.
 * Unicorn executes generated cache pages; original PRX pages are data-only.
 * Native helpers run after uc_emu_start returns, never from an engine hook.
 */
#include "runtime.h"
#include <unicorn/unicorn.h>
#include <unicorn/mips.h>
#include <stdlib.h>

#define CODE_BEGIN UINT32_C(0x09B80000)
#define CODE_END   UINT32_C(0x09C00000)
#define READ_WRITE (UC_PROT_READ | UC_PROT_WRITE)

typedef struct generated_engine {
    uc_engine *uc;
    uc_hook trace;
    rp_context *context;
    uint32_t published_end;
    uint64_t outside_published_code;
} generated_engine;

static void checked(rp_context *c, uc_err error, uint32_t address)
{
    if (error == UC_ERR_OK) return;
    fprintf(stderr, "Unicorn: %s at 0x%08X\n", uc_strerror(error), address);
    rp_event(c, "executor_error", "unicorn_api_error", address, (uint32_t)error);
    rp_block(c, "unicorn_execution_support_error", address);
}

static void observe_generated(uc_engine *uc, uint64_t address, uint32_t size, void *opaque)
{
    (void)size;
    generated_engine *engine = opaque;
    if (address >= engine->published_end) {
        engine->outside_published_code = address;
        uc_emu_stop(uc);
        return;
    }
    /* These are hook observations, not a promise of retired-instruction counts. */
    ++engine->context->generated_instructions;
}

static void map_region(rp_context *c, uc_engine *uc, uint32_t base, uint32_t size, void *bytes)
{
    if (size) checked(c, uc_mem_map_ptr(uc, base, size, READ_WRITE, bytes), base);
}

void rp_unicorn_open(rp_context *c)
{
    generated_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) rp_block(c, "unicorn_context_allocation_failed", 0);
    c->generated_engine = engine;
    engine->context = c;
    checked(c, uc_open(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN, &engine->uc), 0);
    checked(c, uc_ctl_set_cpu_model(engine->uc, UC_CPU_MIPS32_24KF), 0);
    const uint64_t helpers[] = {0x89A0, 0x2888, 0x96AC, 0x1A80, 0x1A68, 0x2450, 0x7F00, 0x2648,
                               0x1A90, 0x1AA8, 0x1AC8, 0x1AE4, 0x1DD0,
                               0x2128, 0x2140, 0x2160, 0x2180};
    checked(c, uc_ctl_exits_enable(engine->uc), 0);
    checked(c, uc_ctl_set_exits(engine->uc, helpers, sizeof(helpers) / sizeof(helpers[0])), 0);
    for (unsigned i = 0; i < RP_REGION_COUNT; ++i) {
        const rp_region *region = &c->regions[i];
        if (!region->bytes || !region->size) continue;
        if (region->base == 0 && region->size >= 0x14000) {
            map_region(c, engine->uc, 0, 0x10000, region->bytes);
            map_region(c, engine->uc, 0x14000, region->size - 0x14000, region->bytes + 0x14000);
        } else {
            map_region(c, engine->uc, region->base, region->size, region->bytes);
        }
        if (region->base == 0x08000000 || region->base == 0x04000000)
            map_region(c, engine->uc, region->base | 0x40000000, region->size, region->bytes);
    }
    map_region(c, engine->uc, 0x10000, sizeof(c->scratchpad), c->scratchpad);
    checked(c, uc_mem_protect(engine->uc, CODE_BEGIN, CODE_END - CODE_BEGIN, UC_PROT_ALL), CODE_BEGIN);
    checked(c, uc_hook_add(engine->uc, &engine->trace, UC_HOOK_CODE,
                          observe_generated, engine, CODE_BEGIN, CODE_END - 1), CODE_BEGIN);
    c->generated_executor = "unicorn_MIPS32_24KF";
    rp_event(c, "execution_adapter", "Unicorn_generated_cache_PRX_nonexecutable", CODE_BEGIN, CODE_END);
}

void rp_unicorn_run(rp_context *c)
{
    generated_engine *engine = c->generated_engine;
    if (!engine || !engine->uc) rp_block(c, "unicorn_not_initialized", c->run_pc);
    uc_engine *uc = engine->uc;
    engine->published_end = rp_u32(c, c->gp + 0x1D0);
    engine->outside_published_code = 0;
    if (c->run_pc < CODE_BEGIN || c->run_pc >= engine->published_end)
        rp_block(c, "unicorn_entry_outside_generated_cache", c->run_pc);

    /* Native reconstruction can patch and extend the cache between runs.
     * Its memory is shared, but Unicorn's translated blocks need invalidation.
     */
    checked(c, uc_ctl_remove_cache(uc, (uint64_t)CODE_BEGIN, (uint64_t)CODE_END), CODE_BEGIN);
    for (unsigned i = 0; i < 32; ++i) {
        checked(c, uc_reg_write(uc, UC_MIPS_REG_0 + i, &c->run_gpr[i]), c->run_pc);
        uint64_t bits = c->run_fpr[i];
        checked(c, uc_reg_write(uc, UC_MIPS_REG_F0 + i, &bits), c->run_pc);
    }
    checked(c, uc_reg_write(uc, UC_MIPS_REG_HI, &c->run_hi), c->run_pc);
    checked(c, uc_reg_write(uc, UC_MIPS_REG_LO, &c->run_lo), c->run_pc);
    const uc_err result = uc_emu_start(uc, c->run_pc, 0, 10000000, 100000);
    checked(c, uc_reg_read(uc, UC_MIPS_REG_PC, &c->run_pc), c->run_pc);
    c->run_next_pc = c->run_pc + 4;
    for (unsigned i = 0; i < 32; ++i) {
        checked(c, uc_reg_read(uc, UC_MIPS_REG_0 + i, &c->run_gpr[i]), c->run_pc);
        uint64_t bits = 0;
        checked(c, uc_reg_read(uc, UC_MIPS_REG_F0 + i, &bits), c->run_pc);
        c->run_fpr[i] = (uint32_t)bits;
    }
    checked(c, uc_reg_read(uc, UC_MIPS_REG_HI, &c->run_hi), c->run_pc);
    checked(c, uc_reg_read(uc, UC_MIPS_REG_LO, &c->run_lo), c->run_pc);

    if (engine->outside_published_code)
        rp_block(c, "unicorn_fetch_outside_published_code", (uint32_t)engine->outside_published_code);
    if ((result == UC_ERR_OK || result == UC_ERR_FETCH_PROT) && c->run_pc < c->regions[0].size) {
        rp_event(c, "native_helper_boundary", "Unicorn_returned_before_PRX_execution", c->run_pc, c->run_gpr[4]);
        return;
    }
    checked(c, result, c->run_pc);
    rp_block(c, "unicorn_generated_execution_budget", c->run_pc);
}

void rp_unicorn_close(rp_context *c)
{
    generated_engine *engine = c->generated_engine;
    if (!engine) return;
    if (engine->uc) uc_close(engine->uc);
    free(engine);
    c->generated_engine = NULL;
}
