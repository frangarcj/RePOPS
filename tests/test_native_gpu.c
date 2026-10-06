#include "../src/native/pops_gpu.h"
#include "../src/native/pops_cdrom.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* These state-only packets return zero delay and start without pending events.
 * Scheduler behavior is covered by the separate event tests. */
void rp_pops_schedule_event(rp_context *c, uint32_t event, uint32_t delay)
{ (void)c; (void)event; (void)delay; abort(); }
void rp_pops_remove_event(rp_context *c, uint32_t event)
{ (void)c; (void)event; abort(); }

/* The fixture checks calls to the capture adapter; it does not render the
 * two firmware templates. Their actual bytes are used by the integrated run. */
static unsigned captured_templates;
static uint32_t expected_closed_list;
uint32_t rp_ge_capture_state_list(rp_context *c, uint32_t address, int module_relative)
{
    const unsigned which = captured_templates++ & 1;
    assert(address == (which ? 0x041B9300 : 0xD5008));
    assert(module_relative == (which ? 0 : 1));
    if (expected_closed_list) {
        assert(c->ge_stalled_list == 0);
        assert(rp_u32(c, expected_closed_list) == 0x0F000000);
        assert(rp_u32(c, expected_closed_list + 4) == 0x0C000000);
    }
    ++c->ge_lists_captured; ++c->services;
    return ++c->next_id;
}

static void reset_status(rp_context *c)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 10000);
    rp_core_set_downcount(c, 5000);
    rp_w32(c, RP_GPU_ADDRESS(c, frame_cycle_origin), 1000);
    rp_w32(c, RP_GPU_ADDRESS(c, status), 0x04000000);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0xC100);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_mode), 0xF925);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles), 777);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); assert(c->trace);
    c->gp = 0x10000;
    reset_status(c);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x84034125);
    assert(rp_core_downcount(c) == 4999);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles)) == 777);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles)) == 5001);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status)) == 0x04000000);

    /* Rapid polling debits the elapsed line phase but records pre-debit time. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, frame_cycle_origin), 4000);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), 4995);
    assert(rp_pops_gpu_read(c, 0x1F801814, 5) == 0x04034125);
    assert(rp_core_downcount(c) == 3998);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles)) == 5001);

    reset_status(c);
    rp_core_set_downcount(c, 0);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), 10000);
    (void)rp_pops_gpu_read(c, 0x1814, 2);
    assert(rp_core_downcount(c) == UINT32_MAX);

    reset_status(c);
    rp_w8(c, RP_GPU_ADDRESS(c, interlaced), 1);
    rp_w8(c, RP_GPU_ADDRESS(c, frame_phase), 1);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x84034125);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 1u << 27);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x04034125);

    /* An unimplemented GPUREAD transfer must not return an invented word. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, data_read_cycle_cost), 7);
    rp_w8(c, RP_GPU_ADDRESS(c, read_selector), 16);
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_gpu_read(c, 0x1810, 2);
        assert(!"Unimplemented GPU data path returned");
    }
    assert(strcmp(c->stop_kind, "GPU_VRAM_data_transfer_not_reconstructed") == 0);
    assert(c->stop_address == 0x130BC && rp_core_downcount(c) == 4993);
    c->regions[0] = (rp_region){0, 0x4AE730, calloc(1, 0x4AE730)};
    c->regions[1] = (rp_region){0x09A00000, 0x1000, calloc(1, 0x1000)};
    assert(c->regions[0].bytes && c->regions[1].bytes);
    c->regions[0].bytes[0xD5338 + 8] = 3; /* Four-word flat triangle. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00000);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[0]), 640);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[1]), 480);
    if (setjmp(c->stop) != 0) {
        fprintf(stderr, "Unexpected GPU state boundary: %s\n", c->stop_kind);
        abort();
    }
    rp_pops_gpu_write(c, 0x1810, 0xE1000234);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, draw_mode)) == 0xE234);
    rp_pops_gpu_write(c, 0x1810, 0xE2012345);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, texture_window)) == 0x12345);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_window_size[0])) == 1);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_window_size[1])) == 2);
    rp_pops_gpu_write(c, 0x1810, 0xE3010008);
    rp_pops_gpu_write(c, 0x1810, 0xE4000000 | (200u << 10) | 300u);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, draw_area_intersects_display)) == 1);
    assert(rp_u32(c, 0x49A00000) == 0xD4010008);
    assert(rp_u32(c, 0x49A00004) == (0xD5000000 | (200u << 10) | 300u));
    rp_pops_gpu_write(c, 0x1810, 0xE5000000 | (0x7FEu << 11) | 0x7FF);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, drawing_offset[0])) == 0xFFFF);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, drawing_offset[1])) == 0xFFFE);
    assert(rp_u32(c, 0x49A00008) == 0x3A000009);
    assert(rp_u32(c, 0x49A0000C) == 0x3BBF8000);
    assert(rp_u32(c, 0x49A00010) == 0x3BC04000);
    rp_pops_gpu_write(c, 0x1810, 0xE6000003);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x1800) == 0x1800);
    rp_pops_gpu_write(c, 0x1810, 0x01000000);
    assert(rp_u32(c, 0x49A0001C) == 0xB01BC000);
    assert(rp_u32(c, 0x49A00020) == 0xC4000001);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A00024);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)) == 0);

    /* Three writes stay buffered; the final word reaches the real boundary. */
    rp_pops_gpu_write(c, 0x1810, 0x200000FF);
    rp_pops_gpu_write(c, 0x1810, 0x00010001);
    rp_pops_gpu_write(c, 0x1810, 0x00020002);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)) == 3);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x14000000) == 0);
    if (setjmp(c->stop) == 0) {
        rp_pops_gpu_write(c, 0x1810, 0x00030003);
        assert(!"Unreconstructed triangle claimed complete");
    }
    assert(strcmp(c->stop_kind, "GPU_primitive_packet_not_reconstructed") == 0);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, packet_words[3])) == 0x00030003);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x14000000) == 0);
    rp_pops_gpu_write(c, 0x1814, 0x01000000);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)) == 0);
    /* Reset with an existing list preserves its two terminator words before
     * the template queues, and leaves a newly stalled list after draw-sync. */
    reset_status(c);
    expected_closed_list = 0x49A00080;
    rp_w32(c, RP_GPU_ADDRESS(c, list_id), 77);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), expected_closed_list);
    rp_w32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS, 1);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0xA5B6C7D8);
    rp_w8(c, RP_GPU_ADDRESS(c, packet_word_count), 11);
    rp_w8(c, RP_GPU_ADDRESS(c, command_mode), 9);
    rp_w8(c, RP_GPU_ADDRESS(c, ge_transfer_pending), 0xFF);
    rp_w32(c, RP_GPU_ADDRESS(c, texture_window), 0x45678);
    c->next_id = 100;
    const unsigned old_services = c->services;
    if (setjmp(c->stop) != 0) {
        fprintf(stderr, "Unexpected GP1 boundary: %s\n", c->stop_kind);
        abort();
    }
    rp_pops_gpu_write(c, 0x1F801814, 0);
    assert(captured_templates == 2 && c->ge_lists_captured == 2);
    assert(c->services == old_services + 5);
    assert(rp_u32(c, RP_GPU_DISPLAY_TRANSITION_ADDRESS) == 2);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_id)) == 103);
    assert(c->ge_stalled_list == 0x49A00000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A00000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status)) == 0x1C800000);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, draw_mode)) == 0xC000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, horizontal_range)) == 0x0C000200);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, vertical_range)) == 0x01000010);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, display_mode)) == 0x00B600D8);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, texture_window)) == 0x45678);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)));
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)));

    rp_pops_gpu_write(c, 0x1814, 0x03000000);
    assert(!(rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x800000));
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, previous_field)) == 2);
    rp_pops_gpu_write(c, 0x1814, 0x060A0020);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, horizontal_range)) == 0x00A00020);
    rp_w8(c, RP_GPU_ADDRESS(c, display_dirty), 0);
    rp_pops_gpu_write(c, 0x1814, 0x060A0020);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, display_dirty)));
    rp_pops_gpu_write(c, 0x1814, 0x07040010);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, vertical_range)) == 0x01000010);
    rp_pops_gpu_write(c, 0x1814, 0x08000028);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, display_mode_bytes[1])) == 0x28);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, display_dirty)) == 2);
    rp_pops_gpu_write(c, 0x1814, 0x10000017);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == 7);
    rp_w32(c, RP_GPU_ADDRESS(c, data_read_cycle_cost), 7);
    const uint32_t before_query = rp_core_downcount(c);
    assert(rp_pops_gpu_read(c, 0x1810, 2) == 0);
    assert(rp_core_downcount(c) == before_query - 7);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, data_read_latch)) == 0);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == 0);
    const uint32_t queries[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 15};
    const uint32_t answers[] = {0, 1, 0x45678, 0x00105006, 0x0001000F,
                                0x003FF7FF, 6, 0, 8, 15};
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_start[0]), 6);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_start[1]), 0x414);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[0]), 15);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[1]), 64);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, drawing_offset[0]), 0xFFFF);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, drawing_offset[1]), 0xFFFE);
    for (unsigned i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        rp_pops_gpu_write(c, 0x1814, 0x10000000 | queries[i]);
        assert(rp_pops_gpu_read(c, 0x1810, 2) == answers[i]);
        assert(rp_u32(c, RP_GPU_ADDRESS(c, data_read_latch)) == answers[i]);
        assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == answers[i]);
    }

    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("GPU: status, GP0 state words, GP1 reset/list ordering and display controls passed; rasterization pending.");
    return 0;
}
