#include "pops_gpu.h"
#include "pops_cdrom.h"
#include "pops_dma.h"
#include <stdlib.h>
#include <string.h>

#define GPU32(member) rp_u32(c, RP_GPU_ADDRESS(c, member))
#define GPU8(member) rp_cd_u8(c, RP_GPU_ADDRESS(c, member))
#define GPU16(member) rp_cd_u16(c, RP_GPU_ADDRESS(c, member))
#define SET_GPU8(member, value) rp_w8(c, RP_GPU_ADDRESS(c, member), (uint8_t)(value))
#define SET_GPU16(member, value) rp_cd_w16(c, RP_GPU_ADDRESS(c, member), (uint16_t)(value))
#define SET_GPU32(member, value) rp_w32(c, RP_GPU_ADDRESS(c, member), (uint32_t)(value))

static uint32_t emit_ge_word(rp_context *c, uint32_t cursor, uint32_t word)
{
    rp_w32(c, cursor, word);
    rp_event(c, "GPU_GE_word", "emitted_not_rendered", cursor, word);
    return cursor + 4;
}

/* POPS +0x12624/+0x128C8 call POPSMAN's 7014C540 with cursor+4. This
 * headless adapter models its enqueue/sync fallback, not the MMIO fast path:
 * FINISH closes the old list, END starts the stalled continuation. Neither
 * the returned synthetic id nor these writes imply that GE work rendered. */
static void capture_ge_boundary(rp_context *c, uint32_t boundary)
{
    const uint32_t old_id = GPU32(list_id);
    (void)emit_ge_word(c, boundary - 4, 0x0F000000);
    (void)emit_ge_word(c, boundary, 0x0C000000);
    c->ge_stalled_list = boundary;
    ++c->services;
    SET_GPU32(list_id, ++c->next_id);
    rp_event(c, "headless_adapter", "POPSMAN_7014C540_submit_captured_not_rendered",
             old_id, GPU32(list_id));
}

void rp_pops_gpu_submit_pending_list(rp_context *c)
{
    if ((int8_t)GPU8(ge_transfer_pending) <= 0) return;
    const uint32_t boundary = GPU32(list_cursor) + 4;
    SET_GPU8(ge_transfer_pending, 0);
    SET_GPU32(list_cursor, boundary);
    capture_ge_boundary(c, boundary);
}

/* +0x15650: drawing area changes also update display-intersection policy. */
static void update_draw_area_policy(rp_context *c)
{
    const int x0 = (int16_t)GPU16(draw_area_start[0]);
    const int y0 = (int16_t)GPU16(draw_area_start[1]);
    const int x1 = (int16_t)GPU16(draw_area_end[0]);
    const int y1 = (int16_t)GPU16(draw_area_end[1]);
    const int x = GPU16(display_origin[0]), y = GPU16(display_origin[1]);
    const int w = GPU16(display_size[0]), h = GPU16(display_size[1]);
    bool large = w < x1 - x0 || h < y1 - y0;
    if (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 1) large = false;
    const bool intersects = x <= x1 && x0 < x + w &&
        ((y <= y1 && y0 < y + h) || (y - 512 <= y1 && y0 < y + h - 512));
    SET_GPU8(draw_area_exceeds_display, large);
    SET_GPU8(draw_area_intersects_display, intersects);
}

/* +0x15554..+0x157C0: GP0 drawing-environment commands, not rasterization. */
static uint32_t drawing_environment(rp_context *c, uint32_t word, uint32_t out)
{
    const unsigned command = (word >> 24) & 31;
    if (command == 1) {
        const uint32_t old = GPU16(draw_mode);
        uint32_t mode = word & 0x7FF;
        if ((mode ^ old) & 0xC1FF) mode |= 0xC000;
        mode |= old & 0x2000;
        SET_GPU16(draw_mode, mode);
        SET_GPU8(draw_mode_gate, (mode & 0x400) ? 0 : GPU8(display_mode_gate) & 1);
    } else if (command == 2) {
        const uint32_t window = word & 0xFFFFF;
        if (window == GPU32(texture_window)) return out;
        const uint32_t xm = word | 32, ym = (word >> 5) | 32;
        const uint32_t width = xm & (0u - xm), height = ym & (0u - ym);
        SET_GPU32(texture_window, window);
        SET_GPU8(texture_window_offset[0], (word >> 10) & (0u - width) & 31);
        SET_GPU8(texture_window_offset[1], (word >> 15) & (0u - height) & 31);
        SET_GPU8(texture_window_size[0], width);
        SET_GPU8(texture_window_size[1], height);
        SET_GPU16(draw_mode, GPU16(draw_mode) | 0x8000);
    } else if (command == 3 || command == 4) {
        if (command == 3) {
            SET_GPU16(draw_area_start[0], word & 0x3FF);
            SET_GPU16(draw_area_start[1], (word >> 10) & 0x1FF);
        } else {
            SET_GPU16(draw_area_end[0], word & 0x3FF);
            SET_GPU16(draw_area_end[1], (word >> 10) & 0x1FF);
        }
        out = emit_ge_word(c, out, (command == 3 ? 0xD4000000u : 0xD5000000u) | (word & 0x7FFFF));
        update_draw_area_policy(c);
    } else if (command == 5) {
        int32_t x = (int32_t)((word & 0x7FF) ^ 0x400) - 0x400;
        const int32_t y = (int32_t)(((word >> 11) & 0x7FF) ^ 0x400) - 0x400;
        if ((int16_t)GPU16(draw_area_start[0]) - x >= 1024) x += 2048;
        out = emit_ge_word(c, out, 0x3A000009);
        const int32_t positions[] = {x, y};
        for (unsigned i = 0; i < 2; ++i) {
            const float position = (float)(positions[i] * 2 + 1);
            uint32_t bits; memcpy(&bits, &position, sizeof(bits));
            bits += 0x3B;
            out = emit_ge_word(c, out, (bits >> 8) | (bits << 24));
        }
        SET_GPU16(drawing_offset[0], x); SET_GPU16(drawing_offset[1], y);
    } else if (command == 6) {
        SET_GPU32(status, (GPU32(status) & ~UINT32_C(0x1800)) | ((word & 3) << 11));
        out = emit_ge_word(c, out, 0x13041B91);
        out = emit_ge_word(c, out, 0x0A000000 | ((word & 3) << 4));
    }
    return out;
}

/* Shared 3+3+3+3+3+1 cache traversal from fill and polygon consumers.
 * Only polygons mark the selected texture page stale when it overlaps. */
static void invalidate_rectangle_cache(rp_context *c, uint32_t x, uint32_t y,
                                        uint32_t right, uint32_t bottom, bool texture)
{
    const uint32_t first = (x >> 6) & 15;
    uint32_t end = ((right + 191) >> 6) & 15;
    uint32_t begin = first - (uint32_t)(int32_t)(int8_t)GPU8(texture_cache[first].group_offset);
    const int32_t right_group = (int8_t)GPU8(texture_cache[end].group_offset);
    if (right_group != 3) end -= (uint32_t)right_group;
    if (begin && begin == first && begin != end) begin -= 3;
    if ((int32_t)(right - x) > 960) end = begin;
    const uint32_t selected = GPU16(draw_mode) & 31;
    uint32_t bank = (y >> 8) & 1;
    const uint32_t last_bank = ((bottom + 255) >> 8) & 1;
    do {
        uint32_t group = begin;
        unsigned visited = 0;
        do {
            if (group > 15 || ++visited > 6)
                rp_block(c, "GPU_fill_cache_group_domain", 0x135E8);
            const uint32_t entry = bank * 16 + group;
            if (texture && entry <= selected && selected <= entry + 2)
                SET_GPU16(draw_mode, GPU16(draw_mode) | 0x8000);
            SET_GPU8(texture_cache[entry].cache_flags, 0);
            if (group == 15) group = 0;
            else {
                if (group > 12) rp_block(c, "GPU_fill_cache_group_domain", 0x135F8);
                SET_GPU8(texture_cache[entry + 1].cache_flags, 0);
                SET_GPU8(texture_cache[entry + 2].cache_flags, 0);
                group += 3;
            }
        } while (group != end);
        bank ^= 1;
    } while (bank != last_bank);
}

/* +0x134E0..+0x136C8: temporary fill scissor, cache invalidation and the
 * original GE clear template, followed by restoration of the draw scissor. */
static uint32_t fill_rectangle(rp_context *c, uint32_t packet, uint32_t *cursor)
{
    const uint32_t extent = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_fill_packet_layout, extent));
    const uint32_t width = extent & 0x3FF, height = (extent >> 16) & 0x1FF;
    if (!width || !height) return 0;
    const uint32_t origin = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_fill_packet_layout, origin));
    const uint32_t command = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_fill_packet_layout, command));
    uint32_t x = origin & 0x3FF;
    const uint32_t y = (origin >> 16) & 0x1FF;
    if (!(rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 0x01000000)) x &= 0x3F0;
    const uint32_t bottom = y + height;
    uint32_t out = emit_ge_word(c, *cursor, 0xD4000000 | (y << 10) | x);
    out = emit_ge_word(c, out, 0xD5000000 | ((bottom - 1) << 10) | (x + width - 1));
    invalidate_rectangle_cache(c, x, y, x + width, bottom, false);

    const bool in_x = x - GPU16(display_origin[0]) < GPU16(display_size[0]);
    if (in_x && (y - GPU16(display_origin[1]) < GPU16(display_size[1]) ||
                 bottom - 1 - GPU16(display_origin[1]) < GPU16(display_size[1])))
        SET_GPU8(previous_field, 1);
    out = emit_ge_word(c, out, 0x55000000 | (command & 0xF8F8F8));
    out = emit_ge_word(c, out, 0x13041B90);
    out = emit_ge_word(c, out, 0x0A000040);
    const uint32_t x0 = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[0]);
    const uint32_t y0 = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[1]);
    const uint32_t x1 = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[0]);
    const uint32_t y1 = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[1]);
    out = emit_ge_word(c, out, 0xD4000000 | (y0 << 10) | x0);
    out = emit_ge_word(c, out, 0xD5000000 | (y1 << 10) | x1);
    *cursor = out;
    const uint32_t cost = (width * height) >> ((GPU8(draw_mode_gate) + 4) & 31);
    rp_event(c, "milestone", "GPU_fill_GE_template_emitted", (width << 16) | height, cost);
    return cost;
}

/* +0x14FFC..+0x152FC: VRAM copy through the original GE transfer template.
 * The overlapping single-row branch stages through PSP EDRAM at +0xD0000.
 * Copies requiring CPU access after GE synchronization remain separate. */
static uint32_t copy_rectangle(rp_context *c, uint32_t packet, uint32_t *cursor)
{
    const uint32_t raw_source = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_copy_packet_layout, source));
    const uint32_t raw_destination = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_copy_packet_layout, destination));
    if (raw_source == raw_destination) return 0;
    SET_GPU32(copy_source, raw_source & 0x01FF03FF);
    SET_GPU32(copy_destination, raw_destination & 0x01FF03FF);
    const uint32_t extent = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_copy_packet_layout, extent));
    const uint32_t width = ((extent - 1) & 0x3FF) + 1;
    const uint32_t height = (((extent >> 16) - 1) & 0x1FF) + 1;
    const int32_t shift = (int32_t)GPU32(copy_cost_shift);
    const uint32_t work = shift < 0 ? (width * height) << ((0u - (uint32_t)shift) & 31) :
                                     (width * height) >> ((uint32_t)shift & 31);
    const int32_t sx = GPU16(copy_source[0]), sy = GPU16(copy_source[1]);
    const int32_t dx = GPU16(copy_destination[0]), dy = GPU16(copy_destination[1]);
    const bool in_x = (uint32_t)dx - GPU16(display_origin[0]) < GPU16(display_size[0]);
    if (in_x && ((uint32_t)dy - GPU16(display_origin[1]) < GPU16(display_size[1]) ||
                 (uint32_t)dy + height - 1 - GPU16(display_origin[1]) < GPU16(display_size[1])))
        SET_GPU8(previous_field, 1);
    const bool mask = (GPU32(status) & 0x800) != 0;
    const bool overlap = dx - (int32_t)width < sx && sx < dx + (int32_t)width &&
                         dy - (int32_t)height < sy && sy < dy + (int32_t)height;
    const uint32_t right = (uint32_t)(sx > dx ? sx : dx) + width;
    const uint32_t bottom = (uint32_t)(sy > dy ? sy : dy) + height;
    const bool fallback = overlap || right > 1024 || bottom * 2 > 1024 || mask;
    rp_event(c, "GPU_copy_packet", "source_destination", raw_source, raw_destination);
    rp_event(c, "GPU_copy_packet", "width_height", width, height);
    if (fallback && (mask || height != 1))
        rp_block(c, "GPU_copy_CPU_sync_path_not_reconstructed", 0x1537C);

    const uint32_t size_word = UINT32_C(0xEDFFFBFF) + width + (height << 10);
    uint32_t out = emit_ge_word(c, *cursor, 0xEB000000 | ((uint32_t)sy << 10) | (uint32_t)sx);
    if (fallback) {
        SET_GPU16(draw_mode, GPU16(draw_mode) | 0xC000);
        out = emit_ge_word(c, out, 0xEC0D0000);
        out = emit_ge_word(c, out, size_word);
        out = emit_ge_word(c, out, 0x13041B90);
        out = emit_ge_word(c, out, 0x0A000000);
        out = emit_ge_word(c, out, 0xEB0D0000);
    }
    out = emit_ge_word(c, out, 0xEC000000 | ((uint32_t)dy << 10) | (uint32_t)dx);
    out = emit_ge_word(c, out, size_word);
    out = emit_ge_word(c, out, 0x13041B90);
    out = emit_ge_word(c, out, 0x0A000000);
    if (width == 1 && height == 1) {
        const unsigned entry = (((unsigned)dy >> 8) & 1) * 16 + (((unsigned)dx >> 6) & 15);
        if (GPU8(texture_cache[entry].cache_flags))
            rp_block(c, "GPU_single_pixel_copy_cache_update_not_reconstructed", 0x15284);
    } else {
        invalidate_rectangle_cache(c, (uint32_t)dx, (uint32_t)dy,
                                   (uint32_t)dx + width, (uint32_t)dy + height, true);
    }
    *cursor = out;
    rp_event(c, "milestone", "GPU_VRAM_copy_GE_prepared_not_executed", fallback, work);
    return work;
}

/* +0x15E70: low pixel first, then the high pixel even across a row boundary.
 * The final halfword can be padding. Logical cursors retain the original
 * completion writes while physical VRAM coordinates wrap independently. */
static bool upload_cpu_word(rp_context *c, uint32_t word)
{
    uint32_t x = GPU16(transfer_cursor[0]), y = GPU16(transfer_cursor[1]);
    const uint32_t end_x = GPU16(upload_end[0]), end_y = GPU16(upload_end[1]);
    const uint16_t mask = (uint16_t)(((GPU32(status) >> 11) & 3) << 15);
    rp_cd_w16(c, rp_gpu_vram_pixel(x, y), (uint16_t)word | mask);
    if (++x == end_x) {
        SET_GPU16(transfer_cursor[1], ++y);
        if (y == end_y) return true;
        x = GPU16(transfer_origin[0]);
    }
    rp_cd_w16(c, rp_gpu_vram_pixel(x, y), (uint16_t)(word >> 16) | mask);
    if (++x == end_x) {
        SET_GPU16(transfer_cursor[1], ++y);
        if (y == end_y) return true;
        x = GPU16(transfer_origin[0]);
    }
    SET_GPU16(transfer_cursor[0], x);
    return false;
}

/* +0x157D8..+0x15E6C selects GE transfers or the original CPU upload path.
 * Partial single rows can stay in mode 9; partial multi-row, mask or X wrap
 * initializes mode 10 after the provider synchronization boundary. */
static uint32_t upload_pixels(rp_context *c, uint32_t source, uint32_t available,
                              uint32_t *cursor, uint32_t *consumed, unsigned *mode)
{
    const uint32_t extent = GPU32(transfer_size);
    uint32_t width = ((extent - 1) & 0x3FF) + 1;
    const uint32_t height = (((extent >> 16) - 1) & 0x1FF) + 1;
    const uint32_t x = GPU16(transfer_origin[0]), y = GPU16(transfer_origin[1]);
    uint32_t words = (width * height + 1) >> 1;
    const bool partial = available < words * 4;
    if (x - GPU16(display_origin[0]) < GPU16(display_size[0]) &&
        y - GPU16(display_origin[1]) < GPU16(display_size[1])) SET_GPU8(previous_field, 1);
    const uint32_t source_x = (source & 15) >> 1;
    if ((partial && height != 1) || (GPU32(status) & 0x800) ||
        width + x > 1024 || width + source_x > 1024) {
        SET_GPU16(upload_end[0], x + width);
        SET_GPU16(upload_end[1], y + height);
        invalidate_rectangle_cache(c, x, y, x + width, y + height, true);
        SET_GPU32(transfer_cursor, GPU32(transfer_origin));
        *cursor += 4;
        capture_ge_boundary(c, *cursor);
        SET_GPU8(ge_transfer_pending, 0);
        *mode = 10;
        *consumed = 0;
        rp_event(c, "GPU_upload", "CPU_path_initialized_after_captured_GE_sync", source, width * height);
        return 0;
    }
    SET_GPU8(ge_transfer_pending, 1);
    if (partial) {
        words = available >> 2;
        SET_GPU16(transfer_size[0], width - words * 2);
        width = words * 2;
    }
    *consumed = words * 4;
    rp_event(c, "host_adapter", "GPU_upload_cache_writeback_coherent_host", source, *consumed);
    uint32_t out = *cursor;
    const uint32_t source_base = 0xB2000000 | (source & 0xFFFFFF);
    const uint32_t source_high = ((source >> 24) & 15) << 16;
    const bool rows = partial || (width & 7) || y + height > 512;
    if (!rows) {
        invalidate_rectangle_cache(c, x, y, x + width, y + height, true);
        out = emit_ge_word(c, out, source_base);
        out = emit_ge_word(c, out, 0xB3000000 | source_high | width);
        out = emit_ge_word(c, out, 0xEB000000 | source_x);
        out = emit_ge_word(c, out, 0xEC000000 | ((y & 511) << 10) | (x & 1023));
        out = emit_ge_word(c, out, UINT32_C(0xEDFFFBFF) + width + (height << 10));
        out = emit_ge_word(c, out, 0x13041B90);
        out = emit_ge_word(c, out, 0x0A000008);
    } else {
        out = emit_ge_word(c, out, 0xB4000000);
        out = emit_ge_word(c, out, 0xB5040400);
        out = emit_ge_word(c, out, 0xB3000400 | source_high);
        out = emit_ge_word(c, out, UINT32_C(0xEDFFFFFF) + width);
        out = emit_ge_word(c, out, source_base);
        uint32_t base = source_base, source_xy = 0xEB000000 + source_x;
        uint32_t destination = 0xEC000000 | ((y & 511) << 10) | (x & 1023);
        for (unsigned row = 0; row < height; ++row) {
            out = emit_ge_word(c, out, destination);
            if ((source_xy & 0x3FF) + width >= 1024) {
                const uint32_t advance = source_xy & 0x3F8;
                base += advance * 2;
                out = emit_ge_word(c, out, base);
                source_xy -= advance;
            }
            out = emit_ge_word(c, out, source_xy);
            out = emit_ge_word(c, out, 0xEA000000);
            destination = (destination + 0x400) & 0xFF07FFFF;
            source_xy += width;
        }
        const bool update_tile = (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 4) &&
            !(((x + width - 1) ^ x) & ~UINT32_C(63)) &&
            !(((y + height - 1) ^ y) & ~UINT32_C(255));
        if (update_tile) {
            const unsigned entry = ((y >> 8) & 1) * 16 + ((x >> 6) & 15);
            if (GPU8(texture_cache[entry].cache_flags)) {
                const uint32_t storage = GPU32(texture_cache[entry].storage_address);
                out = emit_ge_word(c, out, 0xEB000000 | ((y & 511) << 10) | (x & 1023));
                out = emit_ge_word(c, out, 0xB5000100 | (((storage >> 24) & 15) << 16));
                out = emit_ge_word(c, out, 0xB4000000 | (storage & 0xFFFFFF));
                out = emit_ge_word(c, out, 0xEC000000 | ((y & 255) << 10) | (x & 63));
                out = emit_ge_word(c, out, UINT32_C(0xEDFFFBFF) + (width & 0x3FF) + (height << 10));
                out = emit_ge_word(c, out, 0xCB000000);
                out = emit_ge_word(c, out, 0x13041B90);
                out = emit_ge_word(c, out, 0x0A0000C8);
                if (GPU16(texture_cache[entry].group_x_origin)) {
                    if (!entry) rp_block(c, "GPU_upload_cache_predecessor_domain", 0x15B90);
                    SET_GPU8(texture_cache[entry - 1].cache_flags, 0);
                }
            }
        } else invalidate_rectangle_cache(c, x, y, x + width, y + height, true);
    }
    if (partial) SET_GPU16(transfer_origin[0], x + width);
    *mode = partial ? 9 : 0;
    *cursor = out;
    rp_event(c, "GPU_upload", rows ? "GE_rows_pending" : "GE_rectangle_pending", source, words);
    return words;
}

static int32_t coordinate11(uint32_t packed, unsigned shift)
{ return (int32_t)(((packed >> shift) & 0x7FF) ^ 0x400) - 0x400; }
static uint32_t triangle_area2(const rp_gpu_position_layout *a,
                               const rp_gpu_position_layout *b,
                               const rp_gpu_position_layout *d)
{
    const int32_t area = (b->x - a->x) * (d->y - a->y) - (d->x - a->x) * (b->y - a->y);
    return (uint32_t)(area < 0 ? -area : area);
}

/* +0x140A8..+0x14230, then shared +0x13A98: flat untextured primitives.
 * The original GE template jump skips inline vertex data. Z words and the
 * unused fourth vertex of a triangle are deliberately not overwritten. */
static void trace_ge_polygon(rp_context *c, uint32_t body, unsigned count)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("REPOPS_GE_PREVIEW_TRACE");
        enabled = setting && strcmp(setting, "1") == 0;
    }
    if (!enabled) return;
    /* Observe the emitted GE record, not the source GP0 packet. This optional
     * diagnostic never draws or mutates guest state. */
    rp_event(c, "GE_preview", "polygon_begin", body, count);
    rp_event(c, "GE_preview", "frame", GPU32(frame_counter), GPU32(display_mode));
    rp_event(c, "GE_preview", "color_word", body + 4, rp_u32(c, body + 4));
    rp_event(c, "GE_preview", "drawing_offset", 0, GPU32(drawing_offset));
    rp_event(c, "GE_preview", "scissor_min", 0, GPU32(draw_area_start));
    rp_event(c, "GE_preview", "scissor_max", 0, GPU32(draw_area_end));
    for (unsigned i = 0; i < count; ++i)
        rp_event(c, "GE_preview", "vertex_xy", i,
                 rp_u32(c, RP_FIELD_ADDRESS(body, rp_gpu_flat_ge_layout, vertices[i].x)));
    rp_event(c, "GE_preview", "polygon_end", body, count);
}

static uint32_t flat_polygon(rp_context *c, uint32_t packet, unsigned count, uint32_t *cursor)
{
    const uint32_t command = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_flat_packet_layout, command));
    rp_gpu_position_layout vertices[4] = {{0}};
    int32_t min_x = 1024, min_y = 1024, max_x = -1024, max_y = -1024;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t xy = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_flat_packet_layout, positions[i]));
        vertices[i].x = (int16_t)coordinate11(xy, 0);
        vertices[i].y = (int16_t)coordinate11(xy, 16);
        if (vertices[i].x < min_x) min_x = vertices[i].x;
        if (vertices[i].x > max_x) max_x = vertices[i].x;
        if (vertices[i].y < min_y) min_y = vertices[i].y;
        if (vertices[i].y > max_y) max_y = vertices[i].y;
    }
    if (max_x - min_x >= 1024 || 2 * (max_y - min_y) >= 1024)
        rp_block(c, "GPU_oversized_flat_polygon_not_reconstructed", 0x14234);
    uint32_t out = *cursor;
    const uint32_t mode = GPU16(draw_mode);
    if (mode & 0x4000) {
        SET_GPU16(draw_mode, mode - 0x4000);
        out = emit_ge_word(c, out, 0x13041B90);
        out = emit_ge_word(c, out, 0x0A000080 | (((mode >> 5) & 3) << 4));
    }
    const uint32_t body = out;
    out = emit_ge_word(c, out, 0x14000000);
    out = emit_ge_word(c, out, 0x55000000 | (command & 0xFFFFFF));
    (void)emit_ge_word(c, out, (UINT32_C(0x51B7F800) | (((command >> 25) & 15) << 6)) - body);
    for (unsigned i = 0; i < count; ++i) {
        rp_cd_w16(c, RP_FIELD_ADDRESS(body, rp_gpu_flat_ge_layout, vertices[i].x), (uint16_t)vertices[i].x);
        rp_cd_w16(c, RP_FIELD_ADDRESS(body, rp_gpu_flat_ge_layout, vertices[i].y), (uint16_t)vertices[i].y);
    }
    *cursor = body + sizeof(rp_gpu_flat_ge_layout);

    uint32_t area = triangle_area2(&vertices[0], &vertices[1], &vertices[2]);
    if (count == 4) area += triangle_area2(&vertices[3], &vertices[1], &vertices[2]);
    const int32_t dx = (int16_t)GPU16(drawing_offset[0]), dy = (int16_t)GPU16(drawing_offset[1]);
    min_x += dx; max_x += dx; min_y += dy; max_y += dy;
    const int32_t draw_x = (int16_t)GPU16(draw_area_start[0]), draw_y = (int16_t)GPU16(draw_area_start[1]);
    uint32_t work = 30;
    if (max_x <= (int16_t)GPU16(draw_area_end[0]) + 1 && max_y <= (int16_t)GPU16(draw_area_end[1]) + 1) {
        if ((rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 32) && (min_x < draw_x || min_y < draw_y)) area = 0;
        uint32_t weighted = area >> 1;
        if (max_x >= draw_x && max_y >= draw_y && (((command >> 25) & 1) || ((GPU32(status) >> 11) & 2)))
            weighted += area;
        work += weighted >> ((GPU8(draw_mode_gate) + 1) & 31);
    }
    SET_GPU8(previous_field, GPU8(previous_field) | GPU8(draw_area_intersects_display));
    if (GPU8(draw_area_exceeds_display))
        invalidate_rectangle_cache(c, (uint32_t)min_x, (uint32_t)min_y, (uint32_t)max_x, (uint32_t)max_y, true);
    rp_event(c, "milestone", "GPU_flat_polygon_GE_data_prepared_not_rendered", count, work);
    trace_ge_polygon(c, body, count);
    return work;
}

static uint32_t leading_zeroes(uint32_t word)
{ return word ? (uint32_t)__builtin_clz(word) : 32; }

/* +0x148B8..+0x149E0 and cold branches +0x14D28..+0x14E1C.
 * Preserve POPS's indexed-texture conversion lists and cache group tags;
 * the host does not decode or upload a replacement texture here. */
static uint32_t prepare_rectangle_texture(rp_context *c, uint32_t out)
{
    const uint32_t mode = GPU16(draw_mode) & 0x7FF;
    uint32_t depth = (mode >> 7) & 3, stride = 1024, format = depth + 4;
    const uint32_t bank = (mode >> 4) & 1, page = mode & 15;
    uint32_t wx = GPU8(texture_window_offset[0]);
    const uint32_t wy = GPU8(texture_window_offset[1]);
    SET_GPU16(draw_mode, mode);
    out = emit_ge_word(c, out, 0x13041B90);
    out = emit_ge_word(c, out, 0x0A000080 | (((mode >> 5) & 3) << 4));
    uint32_t texture;
    if (!(depth & 2)) {
        wx <<= depth;
        if (!(wx & 3)) {
            const unsigned entry = mode & 31;
            texture = GPU32(texture_cache[entry].storage_address) +
                      ((wx & 1023) | (wy << 10)) * 4;
            if (!GPU8(texture_cache[entry].cache_flags)) {
                const int root = (int)entry - (int8_t)GPU8(texture_cache[entry].group_offset);
                if (root < 0 || root + (page > 11 ? 3 : 2) >= 32)
                    rp_block(c, "GPU_texture_cache_group_domain", 0x14D40);
                for (unsigned i = 0; i < (page > 11 ? 4u : 3u); ++i)
                    SET_GPU8(texture_cache[root + i].cache_flags, format);
                const uint32_t storage = GPU32(texture_cache[root].storage_address);
                const uint32_t origin = (uint32_t)(int32_t)(int16_t)GPU16(texture_cache[root].group_x_origin);
                out = emit_ge_word(c, out, (origin & 0x3FFFF) | ((0x3AC0 + bank) << 18));
                out = emit_ge_word(c, out, 0xB4000000 | (storage & 0xFFFFFF));
                out = emit_ge_word(c, out, 0xB5000000 +
                                   (((storage & 0xFF00003F) | (stride << 6)) >> 8));
                out = emit_ge_word(c, out, 0x0A0000C0);
            }
        } else {
            const uint32_t origin = (((wx + page * 32) * 2) & 0xFFFC1FFF) | ((wy & 31) << 13);
            stride = 32;
            texture = 0x041A0000;
            out = emit_ge_word(c, out, UINT32_C(0xEDFFFC07) + GPU8(texture_window_size[1]) * 8192);
            out = emit_ge_word(c, out, (origin & 0x3FFFF) | ((0x3AC0 + bank) << 18));
            out = emit_ge_word(c, out, 0xB41A0000);
            out = emit_ge_word(c, out, 0xB5040008);
            out = emit_ge_word(c, out, 0x0A0000C4);
        }
        stride >>= depth;
    } else {
        texture = UINT32_C(0x04000000) | (((bank * 32 + wy) * 8 & 511) << 11) |
                  (((page * 8 + wx) * 8 & 1023) << 1);
        depth = 2;
        format = 1;
    }
    if (GPU8(texture_depth) != depth) {
        SET_GPU8(texture_depth, depth);
        out = emit_ge_word(c, out, 0xC3000000 | format);
    }
    const uint32_t zx = leading_zeroes(GPU8(texture_window_size[0]));
    const uint32_t zy = leading_zeroes(GPU8(texture_window_size[1]));
    out = emit_ge_word(c, out, 0xA8000000 + ((stride & 0xFF00FFFF) | ((texture >> 24) << 16)));
    out = emit_ge_word(c, out, 0xA0000000 | (texture & 0xFFFFFF));
    out = emit_ge_word(c, out, UINT32_C(0xB8000322) + ((31 - zy) << 8) - zx);
    out = emit_ge_word(c, out, UINT32_C(0x4AB27F8D) + zx * 0x8000 +
                      (uint32_t)(int32_t)(int16_t)GPU16(texture_offset_word_bias[0]));
    out = emit_ge_word(c, out, UINT32_C(0x4BB27F8E) + zy * 0x8000 +
                      (uint32_t)(int32_t)(int16_t)GPU16(texture_offset_word_bias[1]));
    out = emit_ge_word(c, out, 0xCB000000);
    return emit_ge_word(c, out, 0xCC000000);
}

/* +0x14760..+0x14FC8: rectangle dimensions, work accounting and inline GE
 * sprite records. Their non-obvious Z/UV word overlap is retained exactly. */
static uint32_t rectangle_packet(rp_context *c, uint32_t packet, uint32_t *cursor)
{
    const uint32_t command = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_rectangle_packet_layout, command));
    const uint32_t position = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_rectangle_packet_layout, position));
    const bool textured = (command & 0x04000000) != 0;
    const unsigned size = (command >> 27) & 3;
    uint32_t width, height;
    if (!size) {
        const uint32_t extent_address = textured ?
            RP_FIELD_ADDRESS(packet, rp_gpu_textured_rectangle_packet_layout, extent) :
            RP_FIELD_ADDRESS(packet, rp_gpu_rectangle_packet_layout, extent);
        const uint32_t extent = rp_u32(c, extent_address);
        width = extent & 1023;
        height = (extent >> 16) & 511;
    } else width = height = size == 1 ? 1 : (size - 1) * 8;
    const int32_t off_x = (int16_t)GPU16(drawing_offset[0]), off_y = (int16_t)GPU16(drawing_offset[1]);
    int32_t x = coordinate11(position, 0) + off_x, y = coordinate11(position, 16) + off_y;
    const uint32_t flags = rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags));
    if ((flags & 0x02000000) && height == 1 &&
        (((GPU8(frame_phase) >> 1) ^ (uint32_t)y) & GPU8(draw_mode_gate))) return 30;
    if (x > 1023) x -= 2048;
    if (y > 1023) y -= 2048;
    if (x < -1024) x += 2048;
    if (y < -1024) y += 2048;
    const int32_t draw_x0 = (int16_t)GPU16(draw_area_start[0]), draw_y0 = (int16_t)GPU16(draw_area_start[1]);
    const int32_t draw_x1 = (int16_t)GPU16(draw_area_end[0]), draw_y1 = (int16_t)GPU16(draw_area_end[1]);
    const int32_t right = x + (int32_t)width < draw_x1 ? x + (int32_t)width : draw_x1;
    const int32_t bottom = y + (int32_t)height < draw_y1 ? y + (int32_t)height : draw_y1;
    const int32_t left = x > draw_x0 ? x : draw_x0, top = y > draw_y0 ? y : draw_y0;
    if (right < left || bottom < top) return 30;
    const uint32_t area = (uint32_t)((right - left) * (bottom - top));
    uint32_t weighted = area >> 1;
    if ((command & 0x02000000) || ((GPU32(status) >> 11) & 2)) weighted += area;
    uint32_t work = 30 + (weighted >> (GPU8(draw_mode_gate) & 31));
    SET_GPU8(previous_field, GPU8(previous_field) | GPU8(draw_area_intersects_display));
    const uint32_t vx = (uint32_t)(x - off_x), vy = (uint32_t)(y - off_y);
    uint32_t out = *cursor, color = 0x55000000 | (command & 0xFFFFFF);
    if (!textured) {
        const uint32_t mode = GPU16(draw_mode);
        if (mode & 0x4000) {
            SET_GPU16(draw_mode, mode - 0x4000);
            out = emit_ge_word(c, out, 0x13041B90);
            out = emit_ge_word(c, out, 0x0A000080 | (((mode >> 5) & 3) << 4));
        }
        if (GPU8(draw_area_exceeds_display))
            invalidate_rectangle_cache(c, vx, vy, vx + width, vy + height, true);
        const uint32_t record = out;
        out = emit_ge_word(c, out, 0x14000000);
        out = emit_ge_word(c, out, color);
        out = emit_ge_word(c, out, (UINT32_C(0x51B7FD00) | (((command >> 25) & 3) << 6)) - record);
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_rectangle_ge_layout, vertices[0].x), (vx & 0xFFFF) | (vy << 16));
        rp_cd_w16(c, RP_FIELD_ADDRESS(record, rp_gpu_rectangle_ge_layout, vertices[1].x), (uint16_t)(vx + width));
        rp_cd_w16(c, RP_FIELD_ADDRESS(record, rp_gpu_rectangle_ge_layout, vertices[1].y), (uint16_t)(vy + height));
        out = record + sizeof(rp_gpu_rectangle_ge_layout);
    } else {
        const uint32_t mode = GPU16(draw_mode);
        if (mode != (mode & 0x7FF)) {
            if (mode & 0xC000) out = prepare_rectangle_texture(c, out);
            uint32_t cost = (GPU8(texture_window_size[0]) * GPU8(texture_window_size[1])) << ((GPU8(texture_depth) + 1) & 31);
            if (cost < 128) cost = 0;
            if (cost > area) cost = area;
            if ((flags & 0x00400000) && cost > 1024) {
                cost *= 2;
                SET_GPU16(draw_mode, GPU16(draw_mode) | 0x2000);
            }
            work += cost;
        }
        const uint32_t record = out;
        const uint32_t body = RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, offset_command);
        const uint32_t palette = rp_cd_u16(c, RP_FIELD_ADDRESS(packet, rp_gpu_textured_rectangle_packet_layout, palette));
        out = emit_ge_word(c, out, 0xB0000000 | ((palette & 0x7FFF) << 5));
        out = emit_ge_word(c, out, 0xC4000010);
        if (flags & 0x04000000)
            invalidate_rectangle_cache(c, (uint32_t)x, (uint32_t)y, (uint32_t)x + width, (uint32_t)y + height, false);
        const uint32_t u = rp_cd_u8(c, RP_FIELD_ADDRESS(packet, rp_gpu_textured_rectangle_packet_layout, u));
        const uint32_t v = rp_cd_u8(c, RP_FIELD_ADDRESS(packet, rp_gpu_textured_rectangle_packet_layout, v));
        uint32_t uv = u | (v << 16);
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, vertices[0].u), uv);
        if (!(width & 1) && !(((uv + width - 1) << (GPU8(texture_depth) & 31)) & 15)) uv &= ~1u;
        const uint32_t rotated = (uv >> 16) | (uv << 16);
        const uint32_t end_uv = rotated + (height | (width << 16));
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, vertices[0].x), (vx & 0xFFFF) | (vy << 16));
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, vertices[0].z), end_uv);
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, vertices[1].v), (end_uv & 0xFFFF) | ((vx + width) << 16));
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, vertices[1].y), vy + height);
        if (command & 0x01000000) color = 0x55808080;
        else if (GPU8(texture_depth) & 2) color &= GPU32(texture_color_word_mask);
        const uint32_t scale_u = (leading_zeroes(GPU8(texture_window_size[0])) + 0x906C) << 15;
        const uint32_t scale_v = (leading_zeroes(GPU8(texture_window_size[1])) + 0x926C) << 15;
        out = emit_ge_word(c, out, 0x14000000);
        out = emit_ge_word(c, out, scale_u);
        out = emit_ge_word(c, out, scale_v);
        out = emit_ge_word(c, out, color);
        out = emit_ge_word(c, out, (UINT32_C(0x53B7FD00) | (((command >> 25) & 3) << 6)) - body);
        const uint32_t mask_mode = (GPU32(status) >> 11) & 3;
        bool alternate = (color & 0x808080) == 0x808080;
        if (!alternate && (GPU8(texture_depth) & 2) && ((color | (color >> 8) | (color >> 16)) & 255) > 4) {
            const uint32_t tex_mode = GPU16(draw_mode);
            const int32_t tx = (int32_t)((tex_mode & 15) * 8 + GPU8(texture_window_offset[0]));
            const int32_t ty = (int32_t)((GPU8(texture_window_offset[1]) & ~32u) | (((tex_mode >> 4) & 1) << 5));
            alternate = draw_x0 <= (tx + (int32_t)GPU8(texture_window_size[0])) * 8 && tx * 8 <= draw_x1 &&
                        draw_y0 <= (ty + (int32_t)GPU8(texture_window_size[1])) * 8 && ty * 8 <= draw_y1;
        }
        const uint32_t adjustment = alternate ? UINT32_C(0xFFFFFFC0) : mask_mode < 3 ? 64 : 0;
        out = emit_ge_word(c, out, (UINT32_C(0x51B7FC80) | ((mask_mode & 1) << 2)) + adjustment - body);
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, restore_u), scale_u - 0x40000);
        rp_w32(c, RP_FIELD_ADDRESS(record, rp_gpu_textured_rectangle_ge_layout, restore_v), scale_v - 0x40000);
        out = record + sizeof(rp_gpu_textured_rectangle_ge_layout);
    }
    *cursor = out;
    rp_event(c, "GPU_rectangle", textured ? "textured_GE_record_emitted" : "plain_GE_record_emitted", command, work);
    return work;
}

/* Reached state/fill/flat paths of +0x133D0. GE words are retained in guest RAM;
 * list services remain the existing explicit headless execution adapter. */
static uint32_t consume_packet(rp_context *c, uint32_t source, uint32_t bytes)
{
    rp_function(c, 0x133D0, "pops.consume_GPU_packet_partial");
    uint32_t out = GPU32(list_cursor);
    if (out > 0x49B7B800)
        rp_block(c, "GPU_list_capacity_flush_not_reconstructed", 0x15F2C);
    if ((int8_t)GPU8(ge_transfer_pending) < 0) {
        SET_GPU8(ge_transfer_pending, 0);
        c->services += 2;
        rp_event(c, "headless_adapter", "GE_list_sync_request_captured", GPU32(list_id), 0);
        c->ge_stalled_list = 0x49A00000;
        SET_GPU32(list_id, ++c->next_id);
        rp_event(c, "headless_adapter", "GE_list_queued_at_stall_not_rendered", c->ge_stalled_list, c->next_id);
    }
    unsigned mode = GPU8(command_mode);
    uint32_t work = 0;
    for (uint32_t offset = 0; offset < bytes; offset += 4) {
        const uint32_t word = rp_u32(c, source + offset);
        if (!mode) mode = word >> 29;
        rp_event(c, "GPU_packet", "dispatch", mode, word);
        if (mode == 0) {
            const unsigned command = (word >> 24) & 31;
            if (command == 1) {
                out = emit_ge_word(c, out, 0xB01BC000);
                out = emit_ge_word(c, out, 0xC4000001);
                SET_GPU16(draw_mode, GPU16(draw_mode) | 0x8000);
            } else if (command == 2) {
                if (bytes - offset < sizeof(rp_gpu_fill_packet_layout))
                    rp_block(c, "GPU_fill_truncated_packet", 0x134E0);
                work += fill_rectangle(c, source + offset, &out);
                offset += sizeof(rp_gpu_fill_packet_layout) - 4;
            }
        } else if (mode == 4) {
            if (bytes - offset < sizeof(rp_gpu_copy_packet_layout))
                rp_block(c, "GPU_copy_truncated_packet", 0x14FFC);
            const uint32_t packet = source + offset;
            if (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 64) {
                const uint32_t xy = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_copy_packet_layout, source)) |
                                    rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_copy_packet_layout, destination));
                if (word != 0x80000000 || (xy & 0xFE00FC00)) { mode = 0; break; }
            }
            work += copy_rectangle(c, packet, &out);
            offset += sizeof(rp_gpu_copy_packet_layout) - 4;
        } else if (mode == 5) {
            if (bytes - offset < sizeof(rp_gpu_upload_packet_layout))
                rp_block(c, "GPU_upload_truncated_header", 0x154BC);
            const uint32_t packet = source + offset;
            const uint32_t destination = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_upload_packet_layout, destination));
            const uint32_t extent = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_upload_packet_layout, extent));
            if ((rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 64) &&
                (word != 0xA0000000 || ((destination | extent) & 0xFE00FC00))) { mode = 0; break; }
            SET_GPU32(transfer_origin, destination & 0x01FF03FF);
            SET_GPU32(transfer_size, extent);
            mode = 9;
            offset += sizeof(rp_gpu_upload_packet_layout) - 4;
            rp_event(c, "GPU_upload", "header_waiting_for_pixels", destination, extent);
            continue;
        } else if (mode == 6) {
            /* +0x15508: prepare GPUREAD state, then return immediately.
             * No pixel data or GE completion is fabricated by this header. */
            if (bytes - offset < sizeof(rp_gpu_readback_packet_layout))
                rp_block(c, "GPU_readback_truncated_header", 0x15508);
            const uint32_t packet = source + offset;
            const uint32_t origin = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_readback_packet_layout, source));
            const uint32_t extent = rp_u32(c, RP_FIELD_ADDRESS(packet, rp_gpu_readback_packet_layout, extent));
            const uint32_t width = ((extent - 1) & 0x3FF) + 1;
            const uint32_t height = (((extent >> 16) - 1) & 0x1FF) + 1;
            SET_GPU32(transfer_origin, origin & 0x01FF03FF);
            SET_GPU32(transfer_size, width | (height << 16));
            SET_GPU8(read_selector, 16);
            mode = 0;
            rp_event(c, "GPU_readback", "header_prepared_without_pixels", origin & 0x01FF03FF,
                     width | (height << 16));
            break;
        } else if (mode == 9) {
            uint32_t consumed = 0;
            work += upload_pixels(c, source + offset, bytes - offset, &out, &consumed, &mode);
            if (mode == 10) {
                if (upload_cpu_word(c, word)) mode = 0;
                continue;
            }
            offset += consumed - 4;
            if (mode == 9) break;
        } else if (mode == 10) {
            if (upload_cpu_word(c, word)) {
                mode = 0;
                rp_event(c, "GPU_upload", "CPU_pixels_finished", source + offset, GPU32(transfer_cursor));
            }
            continue;
        } else if (mode == 7) {
            out = drawing_environment(c, word, out);
        } else if (mode == 3) {
            const uint32_t packet_bytes = 8 + ((word & 0x04000000) ? 4 : 0) +
                                          (((word >> 27) & 3) ? 0 : 4);
            if (bytes - offset < packet_bytes)
                rp_block(c, "GPU_rectangle_truncated_packet", 0x14760);
            work += rectangle_packet(c, source + offset, &out);
            offset += packet_bytes - 4;
        } else if (mode == 1 && !(word & 0x14000000)) {
            const unsigned count = word & 0x08000000 ? 4 : 3;
            const uint32_t packet_size = (count + 1) * sizeof(uint32_t);
            if (bytes - offset < packet_size)
                rp_block(c, "GPU_flat_polygon_truncated_packet", 0x140F0);
            work += flat_polygon(c, source + offset, count, &out);
            offset += packet_size - 4;
        } else {
            rp_event(c, "GPU_boundary", "unsupported_packet_mode_and_word", mode, word);
            rp_event(c, "GPU_boundary", "unsupported_packet_source_and_bytes", source + offset, bytes - offset);
            rp_block(c, "GPU_primitive_packet_not_reconstructed", 0x133D0);
        }
        mode = 0;
    }
    SET_GPU8(command_mode, mode);
    SET_GPU32(list_cursor, out);
    return work;
}

/* +0x12C74 linked-list branch. Packet data goes directly to the original
 * +0x133D0 consumer, not through the GP0 port's separate assembly buffer. */
uint32_t rp_pops_gpu_dma_transfer(rp_context *c, uint32_t address, uint32_t bytes, uint32_t control)
{
    rp_function(c, 0x12C74, "pops.GPU_DMA_linked_list_partial");
    if (!(control & 0x400)) {
        if (!(control & 1))
            rp_block(c, "GPU_DMA_VRAM_to_RAM_not_reconstructed", 0x12F90);
        uint32_t source = UINT32_C(0x09800000) | (address & 0x1FFFFF);
        uint32_t available = bytes;
        const uint32_t prefix = GPU8(packet_word_count) * sizeof(uint32_t);
        if (prefix) {
            const uint32_t buffer = RP_GPU_ADDRESS(c, packet_words);
            if (prefix + bytes < sizeof(((rp_core_gpu_layout *)0)->packet_words)) {
                for (uint32_t offset = 0; offset < bytes; offset += 4)
                    rp_w32(c, buffer + prefix + offset, rp_u32(c, source + offset));
                source = buffer;
                available += prefix;
            } else {
                (void)consume_packet(c, buffer, prefix);
            }
            SET_GPU8(packet_extra_words, 0);
            SET_GPU8(packet_word_count, 0);
            rp_event(c, "GPU_DMA", "port_prefix_drained_before_block", prefix, available);
        }
        SET_GPU32(status, GPU32(status) & ~UINT32_C(0x14000000));
        const uint32_t work = consume_packet(c, source, available);
        c->ge_stalled_list = GPU32(list_cursor);
        ++c->services;
        rp_event(c, "headless_adapter", "POPSMAN_E7F06E2B_DMA_stall_captured_not_rendered",
                 c->ge_stalled_list, work);
        uint32_t delay = bytes >> 2;
        uint32_t result = bytes;
        if (GPU32(ready_event.prev)) {
            const uint32_t remaining = GPU32(ready_event.deadline_cycles) - rp_core_guest_cycles(c);
            rp_pops_remove_event(c, RP_GPU_ADDRESS(c, ready_event));
            delay += remaining;
        }
        if (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 0x10) {
            rp_pops_graphics_event(c, 0x125F0);
            delay = 1;
            result = 1;
        }
        rp_pops_schedule_event(c, RP_GPU_ADDRESS(c, ready_event), delay);
        rp_event(c, "GPU_DMA", "block_transfer_consumed", source, bytes);
        return result;
    }
    uint32_t node = UINT32_C(0x09800000) | (address & 0x1FFFFF);
    uint32_t previous = 1, consumed = 0, budget = 0x869, result = 0;
    const uint32_t scaling = GPU32(dma_cost_scaling);
    SET_GPU32(status, GPU32(status) & ~UINT32_C(0x14000000));
    if (GPU32(ready_event.prev)) {
        budget -= GPU32(ready_event.deadline_cycles) - rp_core_guest_cycles(c);
        rp_pops_remove_event(c, RP_GPU_ADDRESS(c, ready_event));
    }
    for (unsigned visited = 0; ; ++visited) {
        if (visited == 65536)
            rp_block(c, "GPU_DMA_chain_diagnostic_budget", node);
        const uint32_t header = rp_u32(c, node);
        const uint32_t words = header >> 24;
        uint32_t extra = 0;
        rp_event(c, "GPU_DMA_packet", "linked_list_payload", node, header);
        if (words) {
            const uint32_t cost = words + 7;
            consumed += cost;
            const uint32_t work = consume_packet(c, node + 4, words * 4);
            const int32_t scaled = (int32_t)(work << ((scaling >> 8) & 31)) >> (scaling & 31);
            const int32_t difference = (int32_t)((uint32_t)scaled - cost);
            if (difference > 0) extra = (uint32_t)difference;
            budget -= extra;
        } else {
            consumed += 4;
        }
        if (header & 0x00800000) {
            budget += extra;
            const int32_t outstanding = (int32_t)(0x869u - budget);
            result = outstanding > 1 ? (uint32_t)outstanding : 1;
            SET_GPU32(status, GPU32(status) | UINT32_C(0x10000000));
            rp_pops_schedule_event(c, RP_GPU_ADDRESS(c, ready_event), result + extra);
            break;
        }
        const uint32_t next = UINT32_C(0x09800000) | (header & 0x1FFFFC);
        result = next + UINT32_C(0x76800000);
        if (next == node || next == previous) {
            budget = UINT32_C(0xFFFF7BB4);
            break;
        }
        previous = node;
        if ((int32_t)budget < (int32_t)consumed && !(rp_u32(c, next) & 0x00800000))
            break;
        node = next;
    }
    const uint32_t horizon = consumed - budget + 0x869;
    /* POPSMAN E7F06E2B writes the GE stall register. This remains a captured
     * host service, not a reconstructed provider body or a rendered list. */
    ++c->services;
    c->ge_stalled_list = GPU32(list_cursor);
    rp_event(c, "headless_adapter", "POPSMAN_E7F06E2B_GE_stall_request", c->ge_stalled_list, 0);
    (void)rp_pops_dma_delay_active(c, 2, consumed, horizon);
    if ((int32_t)horizon < (int32_t)(consumed << 1)) {
        const uint32_t debit = consumed - (uint32_t)((int32_t)(horizon - consumed) >> 3);
        if (GPU8(frame_phase) & 1) {
            const uint32_t field = RP_DMA_ADDRESS(c, deferred_frame_debit);
            rp_w32(c, field, rp_u32(c, field) + debit);
        } else {
            rp_core_set_downcount(c, rp_core_downcount(c) - debit);
        }
    }
    if ((int32_t)result < 0) {
        rp_w32(c, RP_DMA_CHANNEL(c, 2, event.callback), 0x8CAC);
        rp_pops_schedule_event(c, RP_DMA_ADDRESS(c, channels[2]), horizon);
    }
    rp_event(c, "milestone", "GPU_DMA_linked_list_slice_returned", result, consumed);
    return result;
}

/* +0x129B4..+0x12AB8, including the old-list branch +0x12ABC.
 * The reset uses direct sceGe imports, not POPSMAN's 7014C540 helper.
 * Enqueue/sync remain explicit headless services; no pixels are fabricated. */
static void reset_control(rp_context *c)
{
    if (rp_u32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS) == 1)
        rp_w32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS, 2);
    SET_GPU8(ge_transfer_pending, 0);
    const uint32_t old_list = GPU32(list_id);
    if (old_list) {
        const uint32_t cursor = GPU32(list_cursor);
        /* Preserve store order: END is published before FINISH and stall. */
        (void)emit_ge_word(c, cursor + 4, 0x0C000000);
        (void)emit_ge_word(c, cursor, 0x0F000000);
        c->ge_stalled_list = 0;
        ++c->services;
        rp_event(c, "headless_adapter", "sceGeListUpdateStallAddr_request_not_rendered", old_list, 0);
    }
    (void)rp_ge_capture_state_list(c, 0xD5008, 1);
    (void)rp_ge_capture_state_list(c, 0x041B9300, 0);
    SET_GPU32(status, 0x1C800000);
    SET_GPU16(draw_mode, 0xC000);
    SET_GPU16(horizontal_range[0], 0x200); SET_GPU16(horizontal_range[1], 0xC00);
    SET_GPU16(vertical_range[0], 0x10); SET_GPU16(vertical_range[1], 0x100);
    SET_GPU8(display_dirty, 2);
    SET_GPU32(list_cursor, 0x49A00000);
    SET_GPU8(interlaced, 0xFF); SET_GPU8(texture_depth, 0xFF);
    SET_GPU8(display_mode_gate, 0xFF); SET_GPU8(texture_window_size[1], 0x20);
    SET_GPU8(display_mode_bytes[1], 0);
    for (unsigned i = 0; i < 2; ++i) {
        SET_GPU16(draw_area_start[i], 0); SET_GPU16(draw_area_end[i], 0);
        SET_GPU16(drawing_offset[i], 0); SET_GPU16(display_origin[i], 0);
    }
    SET_GPU8(texture_window_size[0], 0x20);
    SET_GPU8(texture_window_offset[0], 0); SET_GPU8(texture_window_offset[1], 0);
    ++c->services;
    rp_event(c, "headless_adapter", "sceGeDrawSync_request_not_rendered", 0, c->ge_lists_captured);
    c->ge_stalled_list = 0x49A00000;
    SET_GPU32(list_id, ++c->next_id);
    ++c->services;
    rp_event(c, "headless_adapter", "sceGeListEnQueue_empty_at_stall", c->ge_stalled_list, c->next_id);
    SET_GPU8(command_mode, 0); SET_GPU8(packet_extra_words, 0); SET_GPU8(packet_word_count, 0);
    rp_event(c, "milestone", "GP1_reset_state_and_queue_sequence_returned", old_list, GPU32(list_id));
}

/* +0x127D8..+0x12988: each command uses the original extra-word table.
 * A packet becomes ready only after its last word, not on every port write. */
void rp_pops_gpu_write(rp_context *c, uint32_t address, uint32_t word)
{
    rp_function(c, 0x127D8, "pops.gpu_port_write_partial");
    rp_event(c, "GPU_port_write", (address & 4) ? "GP1" : "GP0", address, word);
    if (address & 4) {
        const unsigned op = word >> 24;
        if (op > 16 || op == 2 || (op >= 9 && op <= 15)) return;
        if (op == 0) { reset_control(c); return; }
        if (op == 1) {
            SET_GPU8(command_mode, 0); SET_GPU8(packet_extra_words, 0);
            SET_GPU8(packet_word_count, 0);
            return;
        }
        if (op == 4) {
            SET_GPU32(status, (GPU32(status) & 0x9DFFFFFF) |
                ((word & 3) << 29) | ((uint32_t)((word & 3) != 0) << 25));
            return;
        }
        if (op == 3) {
            const uint32_t old = GPU32(status);
            if (((old >> 23) & 1) != (word & 1)) {
                SET_GPU32(status, (old & ~UINT32_C(0x800000)) | ((word & 1) << 23));
                SET_GPU8(previous_field, 2);
            }
            if (rp_u32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS) == 1)
                rp_w32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS, 2);
            return;
        }
        if (op == 5) {
            const uint32_t x = word & 0x3FF, y = (word >> 10) & 0x1FF;
            if (GPU16(display_origin[0]) == x && GPU16(display_origin[1]) == y) return;
            const int x0 = (int16_t)GPU16(draw_area_start[0]), y0 = (int16_t)GPU16(draw_area_start[1]);
            const int x1 = (int16_t)GPU16(draw_area_end[0]), y1 = (int16_t)GPU16(draw_area_end[1]);
            const int dx = (int)x, dy = (int)y;
            const int w = GPU16(display_size[0]), h = GPU16(display_size[1]);
            SET_GPU8(draw_area_intersects_display, dx <= x1 && x0 < dx + w &&
                     ((dy <= y1 && y0 < dy + h) || (dy - 512 <= y1 && y0 < dy + h - 512)));
            SET_GPU8(previous_field, 2);
            SET_GPU16(display_origin[0], x); SET_GPU16(display_origin[1], y);
            return;
        }
        if (op == 6 || op == 7) {
            const uint32_t begin = word & (op == 6 ? 0xFFF : 0x3FF);
            const uint32_t end = op == 6 ? (word >> 12) & 0xFFF : (word >> 10) & 0x3FF;
            const uint32_t field = op == 6 ? RP_GPU_ADDRESS(c, horizontal_range) : RP_GPU_ADDRESS(c, vertical_range);
            const bool changed = rp_cd_u16(c, field) != begin || rp_cd_u16(c, field + 2) != end;
            rp_cd_w16(c, field, (uint16_t)begin); rp_cd_w16(c, field + 2, (uint16_t)end);
            if (changed) SET_GPU8(display_dirty, 2);
            return;
        }
        if (op == 8) {
            const uint32_t changed = (GPU8(display_mode_bytes[1]) ^ word) & 0x5F;
            SET_GPU8(display_mode_bytes[1], word);
            if (changed) SET_GPU8(display_dirty, 2);
            return;
        }
        if (op == 16) { SET_GPU8(read_selector, word & 15); return; }
        rp_block(c, "GPU_control_write_not_reconstructed", 0x12988);
    }
    SET_GPU32(status, GPU32(status) & 0xEBFFFFFF);
    uint32_t extra = GPU8(packet_extra_words);
    const unsigned mode = GPU8(command_mode);
    if (!extra) {
        if (!mode) {
            extra = *(uint8_t *)rp_module_memory(c, 0xD5338 + (word >> 26), 1);
            if (word >> 24 == 2) extra = 2;
        } else if (mode == 9) {
            uint32_t pixels = (uint32_t)GPU16(transfer_size[0]) * GPU16(transfer_size[1]);
            if (pixels > 96) pixels = 96;
            extra = ((pixels + 1) >> 1) - 1;
        } else if (mode < 9) {
            extra = (GPU32(transfer_size) >> 28) & 1;
            extra &= (word & 0xF000F000) != 0x50005000;
        }
        SET_GPU8(packet_extra_words, extra);
    }
    const unsigned received = (unsigned)GPU8(packet_word_count) + 1;
    if (received > 48) rp_block(c, "GPU_packet_buffer_domain_not_supported", received);
    SET_GPU32(packet_words[received - 1], word);
    if ((int32_t)extra >= (int32_t)received) {
        SET_GPU8(packet_word_count, received);
        return;
    }
    const uint32_t delay = consume_packet(c, RP_GPU_ADDRESS(c, packet_words), received * 4);
    if (GPU32(ready_event.prev)) {
        rp_core_set_downcount(c, rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline)) - GPU32(ready_event.deadline_cycles));
        rp_pops_remove_event(c, RP_GPU_ADDRESS(c, ready_event));
    }
    if (delay) rp_pops_schedule_event(c, RP_GPU_ADDRESS(c, ready_event), delay);
    else SET_GPU32(status, GPU32(status) | 0x14000000);
    /* +0x128AC tests the mode saved before consume_packet. Flush a port
     * upload before its live source buffer can be reused by the next write. */
    if (mode == 9) rp_pops_gpu_submit_pending_list(c);
    SET_GPU8(packet_extra_words, 0); SET_GPU8(packet_word_count, 0);
}

/* +0x130BC: the scalar query branch, selected by GP1(10h). The original
 * query table returns zero for selector 7; do not substitute another GPU's ID.
 * Selectors 16/17 are transfer modes set elsewhere, not masked GP1 queries. */
static uint32_t read_data_query(rp_context *c)
{
    rp_function(c, 0x130BC, "pops.gpu_data_query_partial");
    const uint32_t selector = GPU8(read_selector);
    uint32_t result = selector;
    switch (selector) {
    case 2: result = GPU32(texture_window); break;
    case 3:
        result = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[0]) |
                 ((uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[1]) << 10);
        break;
    case 4:
        result = (uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[0]) |
                 ((uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[1]) << 10);
        break;
    case 5:
        result = (GPU16(drawing_offset[0]) & 0x7FF) |
                 ((uint32_t)(GPU16(drawing_offset[1]) & 0x7FF) << 11);
        break;
    case 7: result = 0; break;
    case 16: case 17:
        rp_block(c, "GPU_VRAM_data_transfer_not_reconstructed", 0x130BC);
    default: break;
    }
    SET_GPU32(data_read_latch, result);
    SET_GPU32(transfer_read_latch, result);
    rp_event(c, "GPU_register_read", "data_query_and_latches", selector, result);
    return result;
}

/* +0x12FBC..+0x130BC: status reads compose existing state and frame timing.
 * The polling debit and two timestamps are observable firmware behavior;
 * no GPU-ready bit or frame completion is supplied by the host. */
uint32_t rp_pops_gpu_read(rp_context *c, uint32_t address, uint32_t width)
{
    (void)width; /* Original always returns one word, independent of this input. */
    rp_function(c, 0x12FBC, "pops.gpu_register_read_partial");
    if (!(address & 4)) {
        rp_core_set_downcount(c, rp_core_downcount(c) - GPU32(data_read_cycle_cost));
        return read_data_query(c);
    }

    uint32_t remaining = rp_core_downcount(c) - 1;
    const uint32_t deadline = rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline));
    const uint32_t now = deadline - remaining;
    rp_core_set_downcount(c, remaining);
    if (now - GPU32(status_poll_previous_cycles) < 30) {
        const int32_t phase_cycles = (int32_t)((now - GPU32(frame_cycle_origin)) & 0x7FF);
        const int32_t available = (int32_t)remaining;
        const int32_t debit = phase_cycles < available ? phase_cycles : available;
        if (debit > 0) {
            remaining -= (uint32_t)debit;
            rp_core_set_downcount(c, remaining);
        }
    }

    const uint32_t mode = GPU32(display_mode);
    const uint32_t inverted_phase = ~((uint32_t)GPU8(frame_phase));
    uint32_t status = GPU32(status);
    status = (status & ~UINT32_C(0x007E0000)) | (((mode >> 8) & 63) << 17);
    status = (status & ~UINT32_C(0x00010000)) | (((mode >> 14) & 1) << 16);
    status = (status & ~UINT32_C(0x00004000)) | (((mode >> 15) & 1) << 14);
    status = (status & ~UINT32_C(0x7FF)) | (GPU16(draw_mode) & 0x7FF);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), GPU32(status_poll_last_cycles));
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles), now);

    uint32_t field;
    if (!GPU8(interlaced)) {
        field = ((deadline - remaining - GPU32(frame_cycle_origin)) >> 11) & inverted_phase;
    } else {
        field = (uint32_t)((int32_t)inverted_phase >> 1);
        if (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & (UINT32_C(1) << 27))
            field &= inverted_phase;
    }
    status = (status & UINT32_C(0x7FFFFFFF)) | ((field & 1) << 31);
    rp_event(c, "GPU_register_read", "status_from_state_and_guest_cycles", address, status);
    return status;
}
