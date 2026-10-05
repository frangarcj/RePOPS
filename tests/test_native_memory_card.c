#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->regions[0] = (rp_region){0, 0x800000, calloc(1, 0x800000)};
    c->trace = tmpfile();
    assert(c->regions[0].bytes && c->trace);
    c->gp = 0x10000;
    c->diagnostic_skip_ui = 1;
    c->mc_thread_entry = 0x1AA90;
    rp_w32(c, 0x450EBC, UINT32_MAX);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected blocker: %s\n", c->stop_kind);
        return 1;
    }
    rp_pops_mc_worker_start(c);
    assert(c->mc_worker_ready && rp_u32(c, 0x14CC64) == UINT32_MAX);
    assert(rp_u32(c, 0x14D0F0) == 1 && rp_u32(c, 0x14D094) == 0);
    for (unsigned slot = 0; slot < 2; ++slot) {
        const uint32_t group = 0x10CB50 + slot * 0x20088;
        const uint32_t raw = rp_u32(c, 0x4A2C24 + slot * 0x2018);
        assert(raw == group + 0x88);
        assert(*(uint8_t *)rp_memory(c, group, 1) == 1);
        assert(!memcmp(rp_memory(c, raw, 2), "MC", 2));
        for (unsigned sector = 0; sector < 36; ++sector) {
            const uint8_t *bytes = rp_memory(c, raw + sector * 128, 128);
            uint8_t sum = 0;
            for (unsigned j = 0; j < 128; ++j) sum ^= bytes[j];
            assert(sum == 0);
            if (sector >= 1 && sector <= 15) assert(bytes[0] == 0xA0);
            if (sector >= 16) assert(rp_u32(c, raw + sector * 128) == UINT32_MAX);
        }
        uint32_t free_blocks;
        assert(rp_pops_mc_free_blocks(c, slot, &free_blocks) == 0 && free_blocks == 15);
    }
    const uint32_t calls = c->functions;
    rp_pops_mc_worker_start(c);
    assert(c->functions == calls); /* A parked worker is not reinitialized. */
    rp_w32(c, 0x4A0CA4, 0x51);
    uint32_t free_blocks;
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0 && free_blocks == 14);
    rp_w8(c, 0x4A0C24, 'X');
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0); /* Original OR check. */
    rp_w8(c, 0x4A0C25, 'X');
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0x8101002F && free_blocks == 0);
    fclose(c->trace); free(c->regions[0].bytes); free(c);
    puts("Memory-card startup: volatile formatting/checksum/free-block/ready-state tests passed.");
    return 0;
}
