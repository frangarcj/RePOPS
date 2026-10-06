#include "pops_gpu.h"
#include "pops_cdrom.h"
#include "pops_dma.h"
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

/* Reached state-only paths of +0x133D0. GE words are retained in guest RAM;
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
                rp_block(c, "GPU_fill_packet_not_reconstructed", 0x134E0);
            }
        } else if (mode == 7) {
            out = drawing_environment(c, word, out);
        } else {
            rp_block(c, "GPU_primitive_packet_not_reconstructed", 0x133D0);
        }
        mode = 0;
    }
    SET_GPU8(command_mode, mode);
    SET_GPU32(list_cursor, out);
    return 0;
}

/* +0x12C74 linked-list branch. Packet data goes directly to the original
 * +0x133D0 consumer, not through the GP0 port's separate assembly buffer. */
uint32_t rp_pops_gpu_dma_transfer(rp_context *c, uint32_t address, uint32_t bytes, uint32_t control)
{
    rp_function(c, 0x12C74, "pops.GPU_DMA_linked_list_partial");
    if (!(control & 0x400)) {
        rp_event(c, "GPU_DMA_boundary", "block_transfer_bytes", address, bytes);
        rp_block(c, "GPU_DMA_block_transfer_not_reconstructed", 0x12E98);
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
    SET_GPU8(interlaced, 0xFF); SET_GPU8(unknown_3656, 0xFF);
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
