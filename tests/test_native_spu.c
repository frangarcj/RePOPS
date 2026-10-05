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
    fclose(c->trace); free(c->regions[2].bytes); free(c);
    puts("SPU disabled-path smoke checks passed; active mixer and hardware not validated.");
    return 0;
}
