#include "../src/native/pops_scratchpad.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    const uint32_t code = RP_GENERATED_RAM_BEGIN;
    c->regions[0] = (rp_region){code, 0x1000, calloc(1, 0x1000)};
    c->regions[1] = (rp_region){0x09800000, 0x200000, calloc(1, 0x200000)};
    assert(c->regions[0].bytes && c->regions[1].bytes);
    rp_w32(c, RP_CORE_CACHE_ADDRESS(c, ram_code_cursor), code + 0x1000);
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    const uint32_t entries[] = {0x1DD0, 0x2110, 0x2450};
    const uint32_t targets[] = {RP_STORE_SCRATCH_BYTE, RP_STORE_SCRATCH_HALF, RP_STORE_SCRATCH_WORD};
    const uint32_t aliases[] = {0x1F800000, 0x9F800000, 0xBF800000};
    for (unsigned k = 0; k < 3; ++k) {
        const unsigned width = 1u << k;
        const uint32_t offset = 0x400 - width;
        for (unsigned a = 0; a < 3; ++a) {
            memset(rp_memory(c, RP_PS1_SCRATCHPAD(c), 0x400), 0xA5, 0x400);
            rp_w32(c, code, 0x0C000000 | (entries[k] >> 2));
            c->run_pc = entries[k]; c->run_gpr[4] = aliases[a] + offset;
            c->run_gpr[5] = 0x76543210; c->run_gpr[25] = 0xFFFFF987;
            c->run_gpr[31] = code + 8;
            assert(rp_pops_scratchpad_store(c));
            assert(c->run_pc == code + 8 && c->run_next_pc == code + 12);
            assert(c->run_gpr[25] == 0xFFFFF987 && c->run_gpr[6] == 0x4C);
            assert(c->run_gpr[4] == RP_PS1_SCRATCHPAD(c) + offset);
            assert(rp_u32(c, code) == (0x0C000000 | (targets[k] >> 2)));
            assert(c->run_gpr[2] == rp_u32(c, code));
            const uint8_t *p = rp_memory(c, RP_PS1_SCRATCHPAD(c), 0x400);
            assert(p[offset - 1] == 0xA5);
            for (unsigned i = 0; i < width; ++i) assert(p[offset + i] == (uint8_t)(0x76543210u >> (i * 8)));
        }
        c->run_pc = targets[k]; c->run_gpr[4] = 0x1F8003C0;
        const uint64_t revision = c->generated_code_revision;
        assert(rp_pops_scratchpad_store(c));
        assert(c->generated_code_revision == revision); /* No repeated code patch. */
        assert(c->run_gpr[2] == 0x1F800 && c->run_gpr[25] == 0xFFFFF987);
        c->run_pc = entries[k]; c->run_gpr[4] = 0x1F800400;
        assert(!rp_pops_scratchpad_store(c));
        c->run_pc = targets[k]; c->run_gpr[4] = 0x801FFF00;
        assert(rp_pops_scratchpad_store(c));
        assert(c->run_gpr[4] == 0x099FFF00 && c->run_gpr[2] == 0);
        assert(c->run_gpr[25] == 0xFFFFF987);
        for (unsigned i = 0; i < width; ++i)
            assert(*(uint8_t *)rp_memory(c, 0x099FFF00 + i, 1) == (uint8_t)(0x76543210u >> (i * 8)));
        c->run_pc = targets[k]; c->run_gpr[4] = 0x1F801040;
        assert(!rp_pops_scratchpad_store(c)); /* Device fallback remains explicit. */
    }
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("Scratchpad stores: widths, aliases, JAL patch, warm RAM fallback and unchanged cycles passed.");
    return 0;
}
