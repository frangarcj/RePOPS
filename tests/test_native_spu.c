#include "../src/native/runtime.h"
#include "../src/native/pops_state.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); assert(c->trace);
    c->gp = 0x10000;
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 0x20);
    rp_core_set_downcount(c, 0x30);
    assert(rp_core_guest_cycles(c) == 0xFFFFFFF0);
    assert(rp_u32(c, 0x101B0) == 0x30);
    assert(RP_MIXER_ADDRESS(master_volume.right.level) == 0x09FF1342);
    assert(RP_MIXER_ADDRESS(reverb_parameters.comb_gain) == 0x09FF1356);
    assert(rp_capture_address(1, 5) == 0x49F406CA);
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

    /* The inactive entry declines enabled mixing without mutating producer RAM. */
    rp_w32(c, 0x49F401A8, 0x80000000);
    uint8_t *snapshot = malloc(c->regions[2].size); assert(snapshot);
    memcpy(snapshot, c->regions[2].bytes, c->regions[2].size);
    assert(!rp_pops_spu_inactive_sample(c, &output));
    assert(memcmp(snapshot, c->regions[2].bytes, c->regions[2].size) == 0);
    free(snapshot);
    /* The reached active path processes voices and idle postmix, including
     * the disabled reverb cursor maintenance and master-volume publication. */
    rp_w32(c, 0x49F40000, 0x00100020);
    rp_w32(c, 0x49F40004, 0x04000200);
    rp_w32(c, 0x49F40008, 0);
    rp_w32(c, 0x49F40288, 0x80000001); rp_w32(c, 0x49F40280, 1);
    rp_w32(c, 0x49F40284, 1); rp_w8(c, 0x09FF1794, 0xFF);
    rp_w8(c, 0x49F422C2, 0x21); /* Filter 0: the first two nibbles are 1, 2. */
    output = 0xDEADBEEF;
    if (setjmp(c->stop) != 0) {
        fprintf(stderr, "Unexpected active SPU boundary: %s at %x\n", c->stop_kind, c->stop_address);
        abort();
    }
    assert(rp_pops_spu_sample(c, &output));
    assert(output == 0);
    assert(!rp_u32(c, 0x49F40280) && !rp_u32(c, 0x49F40284));
    assert(rp_u32(c, 0x49F40294) == 3);
    assert(*(uint8_t *)rp_memory(c, 0x09FF0858 + 0x1C, 1) == 24);
    assert(rp_u32(c, 0x09FF0858 + 0x2C) == 0x402);
    assert(*(uint8_t *)rp_memory(c, 0x09FF0858 + 0x72, 1) == 0xFF);
    assert(rp_u32(c, 0x09FF0858 + 0x3A) == 0x20001000);
    assert(*(uint8_t *)rp_memory(c, 0x09FF0858, 1) == 0x40);
    assert(rp_u32(c, 0x09FF1368) == 0x49F402C2);
    assert((rp_u32(c, 0x09FF13E8) & 0xFFFF) == 3);

    /* A nonzero interpolation fixture also reaches the real voice-1 capture
     * buffer, rather than testing only zero-filled/reset sample data. */
    memset(c->regions[2].bytes, 0, c->regions[2].size);
    rp_w32(c, 0x49F401A8, 0xC0000000);
    rp_w32(c, 0x49F40180, 0x20003FFF);
    rp_w32(c, 0x49F40288, 0x80000000);
    rp_w32(c, 0x09FF0000, 0x7FFF);
    const uint32_t voice = 0x09FF0858 + 0x74;
    rp_w32(c, voice + 0x24, 0x7FFF0000);
    rp_w32(c, voice + 0x28, 0xFFFFF000);
    rp_w32(c, voice + 0x68, 0x03E80000);
    rp_w32(c, voice, 0x4000);
    rp_w32(c, voice + 0xA, 0x2000);
    assert(rp_pops_spu_sample(c, &output));
    assert((rp_u32(c, 0x49F40AC0) & 0xFFFF) == 998);
    assert(rp_u32(c, 0x49F40204) == 0x20004000);
    assert(output == 0x007C01F2);
    assert(rp_u32(c, 0x49F401B8) == 0x40007FFE);
    assert((rp_u32(c, 0x09FF13E8) & 0xFFFF) == 1);
    rp_w8(c, 0x49F40293, 1);
    output = 0xDEADBEEF;
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_spu_sample(c, &output);
        assert(!"Streaming CD path unexpectedly accepted");
    }
    assert(strcmp(c->stop_kind, "ME_CD_stream_mix_not_reconstructed") == 0);
    assert(output == 0xDEADBEEF);
    fclose(c->trace); free(c->regions[2].bytes); free(c);
    puts("SPU: release, ADPCM, capture, idle postmix, master output and CD boundary passed.");
    return 0;
}
