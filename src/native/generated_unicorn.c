/* Execution support, not a reconstructed POPS function.
 * Unicorn executes generated cache pages; original PRX pages are data-only.
 * Native helpers run after uc_emu_start returns, never from an engine hook.
 */
#include "runtime.h"
#include "pops_state.h"
#include "pops_gte.h"
#include <unicorn/unicorn.h>
#include <unicorn/mips.h>
#include <stdlib.h>

#define CODE_BEGIN RP_GENERATED_BIOS_BEGIN
#define CODE_END RP_GENERATED_BIOS_END
#define RAM_CODE_BEGIN RP_GENERATED_RAM_BEGIN
#define RAM_CODE_END RP_GENERATED_RAM_END
#define FAST_HELPERS UINT32_C(0x07000000)
#define READ_WRITE (UC_PROT_READ | UC_PROT_WRITE)

typedef struct generated_engine {
    uc_engine *uc;
    uc_hook trace, ram_trace;
    uc_hook code_writes[4];
    rp_context *context;
    uint32_t published_end, ram_published_end;
    uint64_t outside_published_code;
    uint64_t synchronized_revision;
    bool synchronized, always_flush;
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
    if (!((address >= CODE_BEGIN && address < engine->published_end) ||
          (address >= RAM_CODE_BEGIN && address < engine->ram_published_end))) {
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

static void observe_code_write(uc_engine *uc, uc_mem_type type, uint64_t address,
                               int size, int64_t value, void *opaque)
{
    (void)uc; (void)type; (void)value;
    generated_engine *engine = opaque;
    rp_generated_code_access(engine->context, (uint32_t)address, (size_t)size);
}

void rp_unicorn_open(rp_context *c)
{
    generated_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) rp_block(c, "unicorn_context_allocation_failed", 0);
    c->generated_engine = engine;
    engine->context = c;
    const char *legacy = getenv("REPOPS_UNICORN_ALWAYS_FLUSH");
    engine->always_flush = legacy && legacy[0] == '1';
    checked(c, uc_open(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN, &engine->uc), 0);
    checked(c, uc_ctl_set_cpu_model(engine->uc, UC_CPU_MIPS32_24KF), 0);
    const uint64_t helpers[] = {0x89A0, 0x2888, 0x91BC, 0x94C4, 0x96AC, 0x1A80, 0x1A68, 0x2450, 0x2468, 0x7F00, 0x2648,
                               0x1A90, 0x1AA8, 0x1AC8, 0x1AE4, 0x1DD0,
                               0x1DE8, 0x1E00, 0x1E20, 0x1E40,
                               0xD088, 0xD1B0, 0x12FBC, 0x127D8,
                               0x2110, 0x2128, 0x2140, 0x2160, 0x2180,
                               0x267C, 0x2694, 0x26B4, 0x26D4, 0x2878, 0x2918, 0x98C4, 0x9C60, 0x9BE0, 0x9158, 0x85F4,
                               RP_GTE_RTPT_FLAGS_HELPER, RP_GTE_RTPT_NO_FLAGS_HELPER,
                               RP_GTE_NCLIP_HELPER, RP_GTE_AVSZ3_HELPER, RP_GTE_AVSZ4_HELPER};
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
    /* POPS eventually specializes hot guest-memory callsites. Give those
     * rehosted callsites direct aliases so long BIOS copy loops stay inside
     * Unicorn instead of round-tripping through C for every byte. */
    /* Unicorn's MIPS CPU applies KSEG translation, so BFC00000 reads land at
     * physical 1FC00000. Keep that physical mirror separate from PRX memory. */
    checked(c, uc_mem_map(engine->uc, 0x1FC00000, 0x80000, READ_WRITE), 0x1FC00000);
    checked(c, uc_mem_write(engine->uc, 0x1FC00000,
                           c->regions[0].bytes + 0x53C20, 0x80000), 0x1FC00000);
    checked(c, uc_mem_protect(engine->uc, 0x1FC00000, 0x80000, UC_PROT_READ), 0x1FC00000);
    checked(c, uc_mem_map(engine->uc, FAST_HELPERS, 0x1000, UC_PROT_ALL), FAST_HELPERS);
    /* Callsite addresses can change class. Each fast path must return to the
     * C dispatcher on a miss, before modifying the guest address. */
    const uint32_t bios_lbu[] = {
        0x7C8264C0, 0x384217F8, 0x14400004, 0,
        0x90820000, 0x03E00008, 0, 0x08000000 | (0x2468 >> 2), 0
    };
    checked(c, uc_mem_write(engine->uc, RP_FAST_BIOS_LBU, bios_lbu,
                           sizeof(bios_lbu)), RP_FAST_BIOS_LBU);
    static const uint32_t ram_helpers[][3] = {
        {RP_FAST_RAM_SB, 0xA0850000, 0x1DD0},
        {RP_FAST_RAM_LBU, 0x90820000, 0x2468},
        {RP_FAST_RAM_LW, 0x8C820000, 0x2128},
        {RP_FAST_RAM_SW, 0xAC850000, 0x2450},
        {RP_FAST_RAM_SH, 0xA4850000, 0x2110},
        {RP_FAST_RAM_LH, 0x84820000, 0x1DE8},
        {RP_FAST_RAM_LHU, 0x94820000, 0x267C}
    };
    for (unsigned i = 0; i < sizeof(ram_helpers) / sizeof(ram_helpers[0]); ++i) {
        const uint32_t code[] = {
            0x7C822DC0, 0x14400004, 0x2406004C, 0x7CC4FD44,
            0x03E00008, ram_helpers[i][1],
            0x08000000 | (ram_helpers[i][2] >> 2), 0
        };
        checked(c, uc_mem_write(engine->uc, ram_helpers[i][0], code,
                               sizeof(code)), ram_helpers[i][0]);
    }
    checked(c, uc_mem_protect(engine->uc, CODE_BEGIN, CODE_END - CODE_BEGIN, UC_PROT_ALL), CODE_BEGIN);
    checked(c, uc_mem_protect(engine->uc, RAM_CODE_BEGIN, RAM_CODE_END - RAM_CODE_BEGIN, UC_PROT_ALL), RAM_CODE_BEGIN);
    checked(c, uc_hook_add(engine->uc, &engine->trace, UC_HOOK_CODE,
                          observe_generated, engine, CODE_BEGIN, CODE_END - 1), CODE_BEGIN);
    checked(c, uc_hook_add(engine->uc, &engine->ram_trace, UC_HOOK_CODE,
                          observe_generated, engine, RAM_CODE_BEGIN, RAM_CODE_END - 1), RAM_CODE_BEGIN);
    const uint64_t write_ranges[][2] = {
        {CODE_BEGIN, CODE_END}, {RAM_CODE_BEGIN, RAM_CODE_END},
        {CODE_BEGIN | 0x40000000, CODE_END | 0x40000000},
        {RAM_CODE_BEGIN | 0x40000000, RAM_CODE_END | 0x40000000}
    };
    for (unsigned i = 0; i < 4; ++i)
        checked(c, uc_hook_add(engine->uc, &engine->code_writes[i], UC_HOOK_MEM_WRITE,
                              observe_code_write, engine, write_ranges[i][0], write_ranges[i][1] - 1),
                (uint32_t)write_ranges[i][0]);
    c->generated_executor = "unicorn_MIPS32_24KF";
    rp_event(c, "execution_adapter", "Unicorn_generated_cache_PRX_nonexecutable", CODE_BEGIN, CODE_END);
}

void rp_unicorn_run(rp_context *c)
{
    generated_engine *engine = c->generated_engine;
    if (!engine || !engine->uc) rp_block(c, "unicorn_not_initialized", c->run_pc);
    uc_engine *uc = engine->uc;
    engine->published_end = rp_u32(c, RP_CORE_CACHE_ADDRESS(c, bios_code_cursor));
    engine->ram_published_end = rp_u32(c, RP_CORE_CACHE_ADDRESS(c, ram_code_cursor));
    engine->outside_published_code = 0;
    if (!((c->run_pc >= CODE_BEGIN && c->run_pc < engine->published_end) ||
          (c->run_pc >= RAM_CODE_BEGIN && c->run_pc < engine->ram_published_end)))
        rp_block(c, "unicorn_entry_outside_generated_cache", c->run_pc);

    /* Native reconstruction can patch and extend the cache between runs.
     * Its memory is shared, but Unicorn's translated blocks need invalidation.
     */
    if (!engine->synchronized || engine->always_flush ||
            engine->synchronized_revision != c->generated_code_revision) {
        checked(c, uc_ctl_remove_cache(uc, (uint64_t)CODE_BEGIN, (uint64_t)CODE_END), CODE_BEGIN);
        checked(c, uc_ctl_remove_cache(uc, (uint64_t)RAM_CODE_BEGIN, (uint64_t)RAM_CODE_END), RAM_CODE_BEGIN);
        engine->synchronized_revision = c->generated_code_revision;
        engine->synchronized = true;
        ++c->generated_cache_invalidations;
    }
    for (unsigned i = 0; i < 32; ++i) {
        checked(c, uc_reg_write(uc, UC_MIPS_REG_0 + i, &c->run_gpr[i]), c->run_pc);
        uint64_t bits = c->run_fpr[i];
        checked(c, uc_reg_write(uc, UC_MIPS_REG_F0 + i, &bits), c->run_pc);
    }
    checked(c, uc_reg_write(uc, UC_MIPS_REG_HI, &c->run_hi), c->run_pc);
    checked(c, uc_reg_write(uc, UC_MIPS_REG_LO, &c->run_lo), c->run_pc);
    const uc_err result = uc_emu_start(uc, c->run_pc, 0, 10000000, 10000000);
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
    for (unsigned i = 0; i < 32; ++i) {
        rp_event(c, "executor_register", "gpr_at_budget", i, c->run_gpr[i]);
        rp_event(c, "executor_register", "fpr_bits_at_budget", i, c->run_fpr[i]);
    }
    if ((c->run_pc >= CODE_BEGIN && c->run_pc < engine->published_end) ||
        (c->run_pc >= RAM_CODE_BEGIN && c->run_pc < engine->ram_published_end)) {
        const uint32_t base = c->run_pc & ~UINT32_C(255);
        for (unsigned i = 0; i < 128; ++i)
            rp_event(c, "executor_code", "word_at_budget", base + i * 4,
                     rp_u32(c, base + i * 4));
    }
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
