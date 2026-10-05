#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ID UINT32_C(0x09E80400)
#define HEADER UINT32_C(0x09E80000)

static rp_context *fixture(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile();
    assert(c->trace);
    c->gp = 0x10000;
    c->regions[0] = (rp_region){0, 0x800000, calloc(1, 0x800000)};
    c->regions[2] = (rp_region){HEADER, 0xC0000, calloc(1, 0xC0000)};
    c->regions[3] = (rp_region){0x04000000, 0x400000, calloc(1, 0x400000)};
    assert(c->regions[0].bytes && c->regions[2].bytes && c->regions[3].bytes);
    memcpy(rp_memory(c, ID, 10), "TEST00001", 10);
    return c;
}

static void dispose(rp_context *c)
{
    fclose(c->trace);
    for (unsigned i = 0; i < RP_REGION_COUNT; ++i) free(c->regions[i].bytes);
    free(c);
}

static void postprocess(void)
{
    const uint32_t inputs[] = {0, 0xFFFF, 0x10000, 0xFFFFFFFF, 0xFFFFFFFE, 0x80000000};
    const uint32_t outputs[] = {0x10100, 0x1010000, 0x10000, 0x10000, 0x10001, 0x8000FFFF};
    rp_context *c = fixture();
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        rp_w32(c, c->gp + 0x6E8, inputs[i]);
        rp_w32(c, c->gp + 0x6D0, 2);
        rp_w32(c, 0x041B9304, 0x12345678);
        rp_pops_config_postprocess(c);
        assert(rp_u32(c, c->gp + 0x6E8) == outputs[i]);
        assert(rp_u32(c, 0x041B9304) == 0x12345678);
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        rp_w32(c, c->gp + 0x6D0, mode);
        rp_pops_config_postprocess(c);
        assert(rp_u32(c, 0x041B9304) == (mode ? 0xE200D9C8u : 0xE2000F01u));
        assert(rp_u32(c, 0x041B9310) == (mode ? 0xE500AEBFu : 0xE500F001u));
    }
    dispose(c);
}

static void configuration(void)
{
    rp_context *c = fixture();
    memset(c->scratchpad, 0xA5, sizeof(c->scratchpad));
    assert(rp_pops_apply_game_config(c, ID, 0, 0x06060001, 0) == 0x80000002u);
    assert(c->scratchpad[0x6B0] == 0xA5);
    assert(rp_pops_apply_game_config(c, 0, 0, 0, 0) == 0);
    assert(rp_u32(c, c->gp + 0x6B0) == UINT32_MAX);
    assert(rp_u32(c, c->gp + 0x6AC) == 0);
    assert(rp_u32(c, c->gp + 0x6C4) == UINT32_MAX); /* null ID skips postprocess */
    assert(rp_pops_apply_game_config(c, ID, 0, 0, 0) == 0);
    assert(rp_u32(c, c->gp + 0x6C4) == 0);
    assert(rp_u32(c, c->gp + 0x6D8) == 2);

    /* A synthetic matching row: one flag word and two indexed settings. */
    rp_w32(c, 0xEF0B0, 0x54455354u ^ 0x1001u);
    rp_w32(c, 0xEF0B4, 3); rp_w32(c, 0xEF0B8, 0xF2000);
    rp_w32(c, 0xF2000, UINT32_MAX); rp_w32(c, 0xF2004, 0x2000);
    rp_w32(c, 0xF2008, 0); rp_w32(c, 0xF200C, 4);
    rp_w32(c, 0xF2010, 8); rp_w32(c, 0xF2014, 1);
    assert(rp_pops_apply_game_config(c, ID, 0, 0x06060000, 0) == 0);
    assert(rp_u32(c, c->gp + 0x6AC) == 0x2000);
    assert(rp_u32(c, c->gp + 0x6D0) == 1);
    assert(rp_u32(c, c->gp + 0x6D8) == 0xFFFF);
    assert(rp_u32(c, 0x041B9304) == 0xE200D9C8u);

    const uint32_t extra = HEADER + 0x2000;
    memset(rp_memory(c, extra, 0x84), 0, 0x84);
    rp_w32(c, extra, 0x1234); rp_w32(c, extra + 4 + 0x18, 9);
    assert(rp_pops_apply_game_config(c, ID, 0x06060001, 0, extra) == 0);
    assert(rp_u32(c, c->gp + 0x6AC) == 0x1234);
    assert(rp_u32(c, c->gp + 0x6C8) == 9);
    rp_w32(c, 0xF2008, 32);
    if (!setjmp(c->stop)) {
        (void)rp_pops_apply_game_config(c, ID, 0, 0, 0);
        assert(!"Expected unsupported index to stop");
    }
    assert(!strcmp(c->stop_kind, "game_config_index_not_supported"));
    dispose(c);
}

static void block_table(void)
{
    rp_context *c = fixture();
    const uint8_t end_msf[] = {0, 2, 0x34}; /* sector 34 => entries 0..2 */
    memcpy(rp_memory(c, HEADER + 0x81B, 3), end_msf, 3);
    rp_w8(c, HEADER + 0x807, 1); rp_w8(c, HEADER + 0x811, 1);
    rp_w32(c, HEADER + 0xBFC, 0x80000);
    for (unsigned i = 0; i < 3; ++i) rp_w32(c, HEADER + 0x4000 + i * 32, 0x100 + i * 0x1000);
    rp_w32(c, HEADER + 0x4004, 0x9300);
    rp_w32(c, HEADER + 0x4024, 0);
    rp_w32(c, HEADER + 0x4044, 0x200);
    assert(rp_pops_finalize_disc_selection(c, 0, 0x10000) == 0);
    assert(rp_u32(c, c->gp + 0x730) == 33);
    assert(rp_u32(c, HEADER + 0x4000) == 0x90100);
    assert(rp_u32(c, HEADER + 0x4018) == UINT32_MAX);
    assert(rp_u32(c, HEADER + 0x4038) == 0);
    assert(rp_u32(c, HEADER + 0x4058) == 0x400);
    dispose(c);
}

int main(void)
{
    postprocess(); configuration(); block_table();
    puts("Native configuration: synthetic config/override/bounds/MSF/index tests passed; not binary equivalence.");
    return 0;
}
