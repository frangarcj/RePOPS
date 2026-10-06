#include "pops_display.h"
#include <string.h>

#define GPU32(member) rp_u32(c, RP_GPU_ADDRESS(c, member))
#define GPU16(member) rp_cd_u16(c, RP_GPU_ADDRESS(c, member))
#define GPU8(member) rp_cd_u8(c, RP_GPU_ADDRESS(c, member))
#define SET_GPU16(member, value) rp_cd_w16(c, RP_GPU_ADDRESS(c, member), (uint16_t)(value))
#define SET_GPU8(member, value) rp_w8(c, RP_GPU_ADDRESS(c, member), (uint8_t)(value))

static uint32_t emit(rp_context *c, uint32_t out, uint32_t word)
{
    rp_w32(c, out, word);
    rp_event(c, "GPU_GE_word", "display_emitted_not_rendered", out, word);
    return out + 4;
}
static int32_t minimum(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t maximum(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t product_q8(int32_t a, int32_t b)
{ return (int32_t)(uint32_t)((int64_t)a * b) >> 8; }
static uint32_t scissor(uint32_t op, int32_t x, int32_t y)
{ return op | ((uint32_t)y << 10) | (uint32_t)x; }

/* +0x1164C..+0x1166C and +0x125A4: select the requested display vcount
 * using the original signed phase accumulator. This does not change guest
 * CPU cycles or synthesize additional GPU work. */
uint32_t rp_pops_display_next_frame(rp_context *c)
{
    uint32_t next = GPU32(frame_counter) + 1;
    if ((rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 8) ||
        !(GPU32(display_mode) & 0x800)) return next;
    const uint32_t phase = GPU32(pal_frame_phase);
    uint32_t delta;
    if ((int32_t)phase >= 0) {
        ++next;
        delta = GPU8(interlaced) ? UINT32_C(0xFFF91797) : UINT32_C(0xFFF91BE2);
    } else {
        delta = GPU8(interlaced) ? 0x1B6CB : 0x1BF4A;
    }
    rp_w32(c, RP_GPU_ADDRESS(c, pal_frame_phase), phase + delta);
    rp_event(c, "milestone", "PAL_frame_target_selected", next, phase + delta);
    return next;
}

/* Active, internal-screen, 16-bit branch inside +0x115B4. The caller retains
 * the common cache maintenance, draw-state restoration and frame lifecycle.
 * GE commands and vertex buffers are produced, not interpreted here. */
uint32_t rp_pops_display_active_lists(rp_context *c, uint32_t out)
{
    const uint32_t mode = GPU32(display_mode);
    if ((mode & 0x1000) || GPU8(external_output))
        rp_block(c, "display_24bit_or_external_path_not_reconstructed", 0x11C6C);
    const unsigned choice = GPU8(display_choice);
    if (choice > 4) rp_block(c, "display_choice_outside_recovered_table", choice);

    rp_display_resolution_layout resolution;
    rp_display_fit_layout fit;
    const unsigned resolution_index = mode & 0x4000 ? 4 : (mode >> 8) & 3;
    memcpy(&resolution, rp_module_memory(c, RP_DISPLAY_RESOLUTION_TABLE +
           resolution_index * sizeof(resolution), sizeof(resolution)), sizeof(resolution));
    memcpy(&fit, rp_module_memory(c, RP_DISPLAY_FIT_TABLE + choice * sizeof(fit), sizeof(fit)), sizeof(fit));
    const unsigned sprites = resolution.width_units16 / 4;
    if (!sprites || sprites > 10 || (resolution.width_units16 & 3))
        rp_block(c, "display_strip_table_domain_not_supported", resolution.width_units16);

    const uint32_t start = out;
    const uint32_t flags = rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags));
    const unsigned pal = (mode >> 11) & 1;
    const unsigned interlace = GPU8(display_mode_gate) & 31;
    const uint32_t source_x = GPU16(display_origin[0]);
    uint32_t source_y = GPU16(display_origin[1]);
    const int32_t source_height = (int32_t)((pal ? 288u : 240u) << interlace);
    SET_GPU8(draw_mode_gate, ((GPU16(draw_mode) >> 10) ^ 1) & interlace);
    SET_GPU16(display_size[0], (uint32_t)resolution.width_units16 * 16);
    SET_GPU16(display_size[1], source_height);

    /* +0x12448: optional field-copy list precedes the ordinary display setup. */
    if ((flags & 0x800) && interlace) {
        const int32_t rows = minimum((int32_t)GPU16(vertical_range[1]) - GPU16(vertical_range[0]), 255);
        const uint32_t field = (GPU8(frame_phase) >> 1) & 1;
        out = emit(c, out, UINT32_C(0xEDFFFBFF) + (GPU16(display_size[0]) >> 1) + (uint32_t)rows * 0x400);
        out = emit(c, out, 0xEB000000 | ((source_y & 0x1FF) << 10) | ((source_x + field * 512) & 0x3FF));
        out = emit(c, out, 0xEC000000 | ((source_y & 0x1FF) << 10) | ((source_x + (field ^ 1) * 512) & 0x3FF));
        out = emit(c, out, 0x13041B90);
        out = emit(c, out, 0x0A00001C);
    }
    out = emit(c, out, 0x13041B92);
    out = emit(c, out, 0x0A000000);

    const int32_t reference_line = pal ? 25 : 16;
    const int32_t y_scale = 300 + fit.y_scale_delta[pal];
    uint32_t texture = source_x * 2;
    int32_t source_end = (int32_t)source_y + source_height;
    if (source_end < 512) {
        source_end -= (int32_t)source_y;
        texture += source_y * 0x800;
        source_y = 0;
    } else {
        out = emit(c, out, 0xC7000001);
    }
    const int32_t vertical_offset = product_q8((int32_t)GPU16(vertical_range[0]) - reference_line, y_scale);
    int32_t top = fit.y_top_offset + vertical_offset;
    int32_t bottom = fit.y_bottom_offset + vertical_offset + 300;
    texture = (texture & 0xFFFFFF) | 0xA0000000;
    out = emit(c, out, texture);

    /* POPSMAN's E7F06E2B publishes this GE stall. Capture the operation,
     * not an invented execution/completion of the newly emitted commands. */
    c->ge_stalled_list = out;
    ++c->services;
    rp_event(c, "headless_adapter", "provider_E7F06E2B_display_stall_captured_not_rendered", out, 0);

    const uint32_t step = resolution.horizontal_step[choice];
    const int32_t horizontal_delta = (int32_t)GPU16(horizontal_range[0]) - (resolution.horizontal_bias + 0x700);
    const int32_t horizontal_scaled = (int32_t)(uint32_t)((int64_t)horizontal_delta * fit.x_scale_q8 + 128) >> 8;
    int32_t left = horizontal_scaled + fit.x_offset + 240;
    uint32_t u = source_x & 7;
    uint32_t dest_x = (uint32_t)left;
    for (unsigned i = 0; i < sprites; ++i) {
        const uint32_t uv = u | (source_y << 16);
        uint32_t xy = (dest_x & 0xFFFF) | ((uint32_t)top << 16);
        rp_w32(c, RP_DISPLAY_SPRITE(i, first.u), uv);
        rp_w32(c, RP_DISPLAY_SPRITE(i, first.x), xy);
        xy += step;
        const uint32_t next_u = uv + 64;
        rp_cd_w16(c, RP_DISPLAY_SPRITE(i, second.u), (uint16_t)next_u);
        rp_cd_w16(c, RP_DISPLAY_SPRITE(i, second.v), (uint16_t)source_end);
        rp_cd_w16(c, RP_DISPLAY_SPRITE(i, second.x), (uint16_t)xy);
        rp_cd_w16(c, RP_DISPLAY_SPRITE(i, second.y), (uint16_t)bottom);
        /* Z/padding words are retained from the original backing. */
        u = next_u & 0x1FF;
        dest_x = xy;
    }

    const int32_t crop_start = (int32_t)rp_u32(c, RP_DISPLAY_CONFIG(c, crop_start[0]));
    const int32_t visible_end = minimum((int32_t)GPU16(vertical_range[1]) - reference_line,
                                      rp_cd_u16(c, RP_DISPLAY_CONFIG(c, vertical_clip_limit)));
    top = maximum(top, maximum(product_q8(crop_start, y_scale) + fit.y_top_offset, 0));
    int32_t bottom_limit = product_q8(visible_end, y_scale) + fit.y_top_offset;
    bottom_limit -= y_scale > 0x122 || (flags & 2) ? 2 : 1;
    bottom_limit = minimum(bottom_limit, 271);
    int32_t right = product_q8((int32_t)GPU16(horizontal_range[1]) - GPU16(horizontal_range[0]) +
                              resolution.width_rounding, fit.x_scale_q8) + left;
    int32_t left_limit = maximum(fit.x_offset - fit.x_scale_q8 * 5 + 240, 0);
    if (flags & UINT32_C(0x40000000)) left_limit = 0;
    left = maximum(left, left_limit);
    right -= resolution.width_units16 < 30;
    int32_t right_limit = fit.x_scale_q8 * 5 + fit.x_offset + 239;
    right_limit = right_limit < 480 ? minimum(right_limit,
        rp_cd_u16(c, RP_DISPLAY_SPRITE(sprites - 1, second.x))) : 479;
    right = minimum(right - 1, right_limit);
    bottom = minimum(bottom, bottom_limit);

    if (GPU8(display_dirty)) {
        SET_GPU8(display_dirty, 0);
        const uint32_t clears[] = {
            scissor(0xD4000000, left, 0), scissor(0xD5000000, right, top), 0x0A0000C8,
            scissor(0xD4000000, left, bottom), scissor(0xD5000000, right, 271), 0x0A0000C8,
            0xD4000000, scissor(0xD5000000, left, 271), 0x0A0000C8,
            scissor(0xD4000000, right, 0), 0x0A0000C4, 0x01002C00
        };
        for (unsigned i = 0; i < sizeof(clears) / sizeof(clears[0]); ++i) out = emit(c, out, clears[i]);
        const bool large = (int)GPU16(display_size[0]) < (int16_t)GPU16(draw_area_end[0]) - (int16_t)GPU16(draw_area_start[0]) ||
                           (int)GPU16(display_size[1]) < (int16_t)GPU16(draw_area_end[1]) - (int16_t)GPU16(draw_area_start[1]);
        SET_GPU8(draw_area_exceeds_display, !(flags & 1) && large);
    }
    rp_function(c, 0x28290, "pops.get_background_level");
    const int32_t background = (int32_t)rp_u32(c, RP_DISPLAY_SETTINGS(background_level));
    if (background < 0) {
        out = emit(c, out, 0x55000000 | ((uint32_t)background * 0x282828 & 0xFFFFFF));
        out = emit(c, out, 0xC9000000);
    }
    const int32_t split = minimum((top + bottom) >> 1, top + 32);
    const uint32_t primitive = 0x04060000 | ((uint32_t)resolution.primitive_vertex_count & 31);
    out = emit(c, out, scissor(0xD4000000, left, top));
    out = emit(c, out, scissor(0xD5000000, right, split));
    out = emit(c, out, primitive);
    out = emit(c, out, scissor(0xD5000000, right, bottom));
    if (resolution.primitive_vertex_count < 0) {
        out = emit(c, out, texture + 0x400);
        out = emit(c, out, 0xCB000000);
        out = emit(c, out, primitive - 12);
        out = emit(c, out, texture);
        out = emit(c, out, 0xCB000000);
    }
    out = emit(c, out, scissor(0xD4000000, left, split + 1));
    out = emit(c, out, 0x01002C00);
    out = emit(c, out, primitive);
    rp_event(c, "milestone", "active_display_GE_vertices_prepared_not_rendered", RP_DISPLAY_SPRITE_BASE, sprites);
    rp_event(c, "milestone", "active_display_GE_list_prepared_not_rendered", start, (out - start) / 4);
    return out;
}
