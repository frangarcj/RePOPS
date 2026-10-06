#include "../src/native/pops_display.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void seed(rp_context *c)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    memset(rp_memory(c, RP_DISPLAY_SPRITE_BASE, 200), 0xA5, 200);
    const rp_display_resolution_layout row = {{64, 64, 64, 64, 64}, 8, 0, 16, 0};
    const rp_display_fit_layout fit = {0, 0, 0, 64, {-44, -44}};
    memcpy(rp_module_memory(c, RP_DISPLAY_RESOLUTION_TABLE, sizeof(row)), &row, sizeof(row));
    memcpy(rp_module_memory(c, RP_DISPLAY_FIT_TABLE, sizeof(fit)), &fit, sizeof(fit));
    rp_w32(c, RP_DISPLAY_SETTINGS(background_level), 0);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_origin[0]), 3);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_origin[1]), 32);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, horizontal_range[0]), 0x700);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, horizontal_range[1]), 0xB00);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, vertical_range[0]), 16);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, vertical_range[1]), 256);
    rp_cd_w16(c, RP_DISPLAY_CONFIG(c, vertical_clip_limit), 240);
    rp_w32(c, RP_DISPLAY_CONFIG(c, crop_start[0]), 0);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0, 0x4AE730, calloc(1, 0x4AE730)};
    c->regions[1] = (rp_region){0x09A00000, 0x2000, calloc(1, 0x2000)};
    c->regions[3] = (rp_region){0x04000000, 0x400000, calloc(1, 0x400000)};
    assert(c->regions[0].bytes && c->regions[1].bytes && c->regions[3].bytes);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected display boundary: %s at %x\n", c->stop_kind, c->stop_address);
        abort();
    }
    const uint32_t start = 0x49A00000;
    seed(c);
    const uint32_t expected[] = {
        0x13041B92, 0x0A000000, 0xA0010006,
        0xD40000F0, 0xD50081DF, 0x04060008, 0xD503BDDF,
        0xD40084F0, 0x01002C00, 0x04060008
    };
    assert(rp_pops_display_active_lists(c, start) == start + sizeof(expected));
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        assert(rp_u32(c, start + i * 4) == expected[i]);
    assert(c->ge_stalled_list == start + 12);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, display_size[0])) == 256);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, display_size[1])) == 240);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, first.u)) == 3);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, first.v)) == 0);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, first.x)) == 240);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, second.u)) == 67);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, second.v)) == 240);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, second.x)) == 304);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, second.y)) == 300);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(3, second.x)) == 496);
    for (unsigned i = 0; i < 4; ++i) {
        assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(i, first.z)) == 0xA5A5);
        assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(i, second.z)) == 0xA5A5);
    }

    seed(c);
    rp_w8(c, RP_GPU_ADDRESS(c, display_dirty), 2);
    rp_w32(c, RP_DISPLAY_SETTINGS(background_level), UINT32_MAX);
    assert(rp_pops_display_active_lists(c, start) == start + 24 * 4);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, display_dirty)));
    assert(rp_u32(c, start + 5 * 4) == 0x0A0000C8);
    assert(rp_u32(c, start + 13 * 4) == 0x0A0000C4);
    assert(rp_u32(c, start + 15 * 4) == 0x55D7D7D8);
    assert(rp_u32(c, start + 16 * 4) == 0xC9000000);

    seed(c);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_origin[1]), 400);
    assert(rp_pops_display_active_lists(c, start) == start + 11 * 4);
    assert(rp_u32(c, start + 8) == 0xC7000001);
    assert(rp_u32(c, start + 12) == 0xA0000006);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, first.v)) == 400);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(0, second.v)) == 640);

    /* A negative primitive-count descriptor splits the wide-mode submission;
     * its width field still describes ten pairs, not forty pairs. */
    seed(c);
    rp_display_resolution_layout wide = {{64, 64, 64, 64, 64}, -16, 0, 40, 0};
    memcpy(rp_module_memory(c, RP_DISPLAY_RESOLUTION_TABLE + 3 * sizeof(wide), sizeof(wide)), &wide, sizeof(wide));
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0x300);
    assert(rp_pops_display_active_lists(c, start) == start + 15 * 4);
    assert(rp_u32(c, start + 7 * 4) == 0xA0010406);
    assert(rp_u32(c, start + 9 * 4) == 0x04060004);
    assert(rp_u32(c, start + 14 * 4) == 0x04060010);
    assert(rp_cd_u16(c, RP_DISPLAY_SPRITE(9, second.x)) == 880);

    seed(c);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0x1000);
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_display_active_lists(c, start);
        assert(!"24-bit transfer was silently accepted");
    }
    assert(strcmp(c->stop_kind, "display_24bit_or_external_path_not_reconstructed") == 0);

    seed(c);
    rp_w32(c, RP_GPU_ADDRESS(c, frame_counter), 100);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0x800);
    rp_core_set_downcount(c, 123);
    const uint32_t targets[] = {102, 103, 104, 105, 106, 108};
    const int32_t phases[] = {-451614, -337108, -222602, -108096, 6410, -445204};
    for (unsigned i = 0; i < 6; ++i) {
        assert(rp_pops_display_next_frame(c) == targets[i]);
        assert((int32_t)rp_u32(c, RP_GPU_ADDRESS(c, pal_frame_phase)) == phases[i]);
        rp_w32(c, RP_GPU_ADDRESS(c, frame_counter), targets[i]);
    }
    assert(rp_core_downcount(c) == 123);
    rp_w8(c, RP_GPU_ADDRESS(c, interlaced), 1);
    rp_w32(c, RP_GPU_ADDRESS(c, pal_frame_phase), 0);
    assert(rp_pops_display_next_frame(c) == 110);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, pal_frame_phase)) == 0xFFF91797);
    assert(rp_pops_display_next_frame(c) == 109);
    assert((int32_t)rp_u32(c, RP_GPU_ADDRESS(c, pal_frame_phase)) == -340382);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 8);
    assert(rp_pops_display_next_frame(c) == 109);
    assert((int32_t)rp_u32(c, RP_GPU_ADDRESS(c, pal_frame_phase)) == -340382);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 0);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0);
    assert(rp_pops_display_next_frame(c) == 109);
    assert((int32_t)rp_u32(c, RP_GPU_ADDRESS(c, pal_frame_phase)) == -340382);
    fclose(c->trace);
    for (unsigned i = 0; i < RP_REGION_COUNT; ++i) free(c->regions[i].bytes);
    free(c);
    puts("Display: GE lists, vertices and original PAL phase/target selection passed; no rendering.");
    return 0;
}
