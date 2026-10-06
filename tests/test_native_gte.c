#include "../src/native/pops_gte.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void half(rp_context *c, uint32_t a, uint16_t v)
{
    rp_w8(c, a, (uint8_t)v); rp_w8(c, a + 1, (uint8_t)(v >> 8));
}
static void identity(rp_context *c)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    for (unsigned i = 0; i < 3; ++i) half(c, RP_GTE_ADDRESS(c, rotation[i][i]), 4096);
    half(c, RP_GTE_ADDRESS(c, projection_distance), 1024);
    c->run_gpr[29] = RP_GTE_RECIPROCAL_ANCHOR;
    /* Exact pinned table value for normalized depth 0x8000. */
    rp_w32(c, RP_GTE_RECIPROCAL_ANCHOR - 0x20000, 0x80000000);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0x097C0000, 0x21000, calloc(1, 0x21000)};
    assert(c->regions[0].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "Unexpected GTE boundary: %s\n", c->stop_kind); abort(); }
    const int16_t vertices[3][3] = {{100,200,1024},{-50,80,1024},{150,-20,1024}};
    for (unsigned checked = 0; checked < 2; ++checked) {
        identity(c);
        c->vfpu_s330_bits = 0x13572468;
        for (unsigned v = 0; v < 3; ++v)
            for (unsigned a = 0; a < 3; ++a)
                half(c, RP_GTE_ADDRESS(c, vectors[v].component[a]), (uint16_t)vertices[v][a]);
        rp_w32(c, RP_GTE_ADDRESS(c, depth[0]), 0xAAAA0000);
        rp_w32(c, RP_GTE_ADDRESS(c, depth[3]), 0xBBBB007B);
        rp_w32(c, RP_GTE_ADDRESS(c, screen_alias), 0xDEADBEEF);
        half(c, RP_GTE_ADDRESS(c, depth_cue_slope), 1);
        rp_pops_gte_rtpt(c, checked != 0);
        for (unsigned v = 0; v < 3; ++v) {
            const uint32_t xy = (uint16_t)vertices[v][0] | ((uint32_t)(uint16_t)vertices[v][1] << 16);
            assert(rp_u32(c, RP_GTE_ADDRESS(c, screen[v])) == xy);
            assert((rp_u32(c, RP_GTE_ADDRESS(c, depth[v + 1])) & 0xFFFF) == 1024);
        }
        assert(rp_u32(c, RP_GTE_ADDRESS(c, depth[0])) == 0xAAAA007B);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, depth[3])) == 0xBBBB0400);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, screen_alias)) == 0xDEADBEEF);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[1])) == 150);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[2])) == (uint32_t)-20);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[3])) == 1024);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[1])) == 150);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[2])) == (uint32_t)-20);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[3])) == 1024);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[0])) == 65536);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[0])) == 16);
        assert(c->run_lo == 65536 && c->run_hi == 0);
        assert(c->vfpu_s330_bits == (checked ? 0 : 0x13572468));
    }
    identity(c);
    rp_w32(c, RP_GTE_ADDRESS(c, translation[0]), 40000);
    rp_w32(c, RP_GTE_ADDRESS(c, translation[1]), (uint32_t)-40000);
    rp_w32(c, RP_GTE_ADDRESS(c, translation[2]), 100);
    half(c, RP_GTE_ADDRESS(c, projection_distance), 1000);
    half(c, RP_GTE_ADDRESS(c, depth_cue_slope), 0x8000);
    rp_w32(c, RP_GTE_ADDRESS(c, depth_cue_intercept), 0x80000000);
    rp_pops_gte_rtpt(c, true);
    assert(rp_u32(c, RP_GTE_ADDRESS(c, screen[2])) == 0xFC0003FF);
    assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[1])) == 32767);
    assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[2])) == (uint32_t)-32768);
    assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[0])) == 0);
    assert(c->vfpu_s330_bits == 0x0182F000);

    identity(c);
    rp_w32(c, RP_GTE_ADDRESS(c, translation[2]), UINT32_MAX);
    rp_pops_gte_rtpt(c, true);
    assert((rp_u32(c, RP_GTE_ADDRESS(c, depth[3])) & 0xFFFF) == 0);
    assert(rp_u32(c, RP_GTE_ADDRESS(c, ir[3])) == UINT32_MAX);
    assert(c->vfpu_s330_bits == 0x60000);

    /* The original discards HI before shifting each rotation result. */
    identity(c);
    for (unsigned a = 0; a < 3; ++a) {
        half(c, RP_GTE_ADDRESS(c, rotation[0][a]), 32767);
        for (unsigned v = 0; v < 3; ++v)
            half(c, RP_GTE_ADDRESS(c, vectors[v].component[a]), 32767);
    }
    /* Force the ratio fallback to isolate MAC truncation from the lookup. */
    rp_w32(c, RP_GTE_ADDRESS(c, translation[2]), (uint32_t)-32767);
    rp_pops_gte_rtpt(c, true);
    assert((int32_t)rp_u32(c, RP_GTE_ADDRESS(c, mac[1])) == -262192);
    const struct {
        int16_t xy[3][2];
        int64_t determinant;
    } triangles[] = {
        {{{1,2},{4,2},{1,6}},12},
        {{{1,2},{1,6},{4,2}},-12},
        {{{1,2},{4,8},{7,14}},0},
        {{{-32768,-32768},{32767,-32768},{-32768,32767}},INT64_C(4294836225)},
        {{{-32768,-32768},{-32768,32767},{32767,-32768}},-INT64_C(4294836225)}
    };
    for (unsigned i = 0; i < sizeof(triangles) / sizeof(triangles[0]); ++i) {
        identity(c);
        for (unsigned v = 0; v < 3; ++v) {
            half(c, RP_GTE_ADDRESS(c, screen[v].x), (uint16_t)triangles[i].xy[v][0]);
            half(c, RP_GTE_ADDRESS(c, screen[v].y), (uint16_t)triangles[i].xy[v][1]);
        }
        uint8_t saved_screen[12];
        memcpy(saved_screen, rp_memory(c, RP_GTE_ADDRESS(c, screen), 12), 12);
        c->vfpu_s330_bits = UINT32_MAX;
        c->run_gpr[9] = 0xA5A5A5A5;
        rp_w32(c, RP_GTE_ADDRESS(c, flags_shadow), 0x87654321);
        rp_pops_gte_nclip(c);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[0])) == (uint32_t)triangles[i].determinant);
        assert(c->run_lo == (uint32_t)triangles[i].determinant);
        assert(c->run_hi == (uint32_t)((uint64_t)triangles[i].determinant >> 32));
        assert(c->run_gpr[4] == c->run_lo && c->run_gpr[28] == c->gp);
        assert(c->run_gpr[9] == 0xA5A5A5A5);
        assert(c->vfpu_s330_bits == 0);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, flags_shadow)) == 0x87654321);
        assert(memcmp(saved_screen, rp_memory(c, RP_GTE_ADDRESS(c, screen), 12), 12) == 0);
    }
    const struct {
        bool four;
        uint16_t depths[4];
        int16_t scale;
        uint16_t expected;
        uint32_t lo, hi, flags;
    } averages[] = {
        {false,{7,1,2,3},4096,6,24576,0,0},
        {true, {7,1,2,3},4096,13,53248,0,0},
        {false,{7,1,2,3},-4096,0,0xFFFFA000,UINT32_MAX,0x40000},
        {false,{0,30000,30000,30000},4096,65535,368640000,0,0x40000},
        {false,{65535,65535,65535,65535},32767,65535,0x7FFB8003,1,0x40000},
        {true, {65535,65535,65535,65535},32767,0,0xFFFA0004,1,0x40000},
        {true, {0,0,0,0},32767,0,0,0,0}
    };
    for (unsigned i = 0; i < sizeof(averages) / sizeof(averages[0]); ++i) {
        identity(c);
        for (unsigned v = 0; v < 4; ++v)
            half(c, RP_GTE_ADDRESS(c, depth[v].value), averages[i].depths[v]);
        half(c, RP_GTE_ADDRESS(c, depth_scale3), (uint16_t)averages[i].scale);
        half(c, RP_GTE_ADDRESS(c, depth_scale4), (uint16_t)averages[i].scale);
        half(c, RP_GTE_ADDRESS(c, ordering_depth_padding), 0xCAFE);
        c->vfpu_s330_bits = UINT32_MAX;
        c->run_gpr[8] = 0x12345678;
        rp_pops_gte_avsz(c, averages[i].four);
        assert(rp_u32(c, RP_GTE_ADDRESS(c, ordering_depth)) == (0xCAFE0000u | averages[i].expected));
        assert(rp_u32(c, RP_GTE_ADDRESS(c, mac[0])) == averages[i].lo);
        assert(c->run_lo == averages[i].lo && c->run_hi == averages[i].hi);
        assert(c->vfpu_s330_bits == averages[i].flags);
        assert(c->run_gpr[8] == 0x12345678);
    }
    fclose(c->trace); free(c->regions[0].bytes); free(c);
    puts("GTE: RTPT, NCLIP and AVSZ3/4 depths, saturation, MAC truncation and preserved state passed.");
    return 0;
}
