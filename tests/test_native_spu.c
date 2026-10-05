#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); assert(c->trace);
    c->regions[2] = (rp_region){0x09F40000, 0xC0000, calloc(1, 0xC0000)};
    assert(c->regions[2].bytes);
    uint32_t output = 0xFFFFFFFF;

    /* Disabled, with nonzero carried state: it must not just return silence. */
    rp_w32(c, 0x49F4018C, 0x12345678);
    rp_w32(c, 0x09FF13E4, 0xABCDEF01);
    assert(rp_pops_spu_inactive_sample(c, &output));
    assert(output == 0x12345678);
    assert(rp_u32(c, 0x09F40294) == 1);
    assert(rp_u32(c, 0x09FF13E8) == 1);
    assert(rp_u32(c, 0x49F4019C) == 0xABCDEF01);

    /* Transition from enabled state resets voice state and its carried word. */
    rp_w32(c, 0x09FF1790, 0x8000);
    memset(rp_memory(c, 0x09FF0858, 0xAE0), 0xA5, 0xAE0);
    assert(rp_pops_spu_inactive_sample(c, &output));
    assert(output == 0);
    assert(rp_u32(c, 0x09FF0858) == 0);
    assert(rp_u32(c, 0x09F40294) == 2);

    /* Enabled mixing is still unsupported and must not mutate producer RAM. */
    rp_w32(c, 0x49F401A8, 0x80000000);
    uint8_t *snapshot = malloc(c->regions[2].size); assert(snapshot);
    memcpy(snapshot, c->regions[2].bytes, c->regions[2].size);
    assert(!rp_pops_spu_inactive_sample(c, &output));
    assert(memcmp(snapshot, c->regions[2].bytes, c->regions[2].size) == 0);
    free(snapshot);
    /* The new active prefix consumes control, but stops rather than claiming
     * that an unreconstructed voice has produced a sample. */
    rp_w32(c, 0x49F40000, 0x00100020);
    rp_w32(c, 0x49F40004, 0x04000200);
    rp_w32(c, 0x49F40008, 0);
    rp_w32(c, 0x49F40288, 1); rp_w32(c, 0x49F40280, 1);
    rp_w32(c, 0x49F40284, 1); rp_w8(c, 0x09FF1794, 0xFF);
    output = 0xDEADBEEF;
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_spu_sample(c, &output);
        assert(!"Active prefix unexpectedly returned a sample");
    }
    assert(strcmp(c->stop_kind, "ME_voice_sample_path_not_reconstructed") == 0);
    assert(c->stop_address == 0x11CC && output == 0xDEADBEEF);
    assert(!rp_u32(c, 0x49F40280) && !rp_u32(c, 0x49F40284));
    assert(rp_u32(c, 0x49F40294) == 3);
    assert(*(uint8_t *)rp_memory(c, 0x09FF0858 + 0x1C, 1) == 24);
    assert(rp_u32(c, 0x09FF0858 + 0x2C) == 0x400);
    assert(*(uint8_t *)rp_memory(c, 0x09FF0858, 1) == 0x40);
    fclose(c->trace); free(c->regions[2].bytes); free(c);
    puts("SPU callback: disabled branch and active control prefix passed; no active sample completed.");
    return 0;
}
