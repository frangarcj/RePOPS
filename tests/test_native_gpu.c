#include "../src/native/pops_gpu.h"
#include "../src/native/pops_cdrom.h"
#include "../src/native/pops_dma.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Scheduler behavior is tested separately. DMA fixtures record its contract;
 * ordinary state-only port packets must not unexpectedly schedule an event. */
static bool dma_fixture;
static unsigned scheduled;
static uint32_t scheduled_event, scheduled_delay, delayed_words, delay_horizon;
void rp_pops_schedule_event(rp_context *c, uint32_t event, uint32_t delay)
{
    (void)c;
    assert(dma_fixture);
    ++scheduled; scheduled_event = event; scheduled_delay = delay;
}
void rp_pops_remove_event(rp_context *c, uint32_t event)
{ (void)c; (void)event; abort(); }
uint32_t rp_pops_dma_delay_active(rp_context *c, uint16_t mask, uint32_t delay, uint32_t horizon)
{
    (void)c;
    assert(dma_fixture && mask == 2);
    delayed_words = delay; delay_horizon = horizon;
    return 0;
}

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

static void check_vram_copy(rp_context *c)
{
    reset_status(c);
    const uint32_t out = 0x49A00400;
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    rp_w32(c, RP_GPU_ADDRESS(c, copy_cost_shift), 2);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[0]), 320);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[1]), 240);
    for (unsigned i = 0; i < 32; ++i) {
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset), (uint8_t)(i % 16 == 15 ? 3 : (i % 16) % 3));
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags), 0xAB);
    }
    c->regions[0].bytes[0xD5338 + 32] = 3;
    dma_fixture = true; scheduled = 0;
    const uint32_t packet[] = {0x80000000, 0x000A0010, 0x001E0041, 0x00080020};
    const uint32_t words[] = {0xEB002810, 0xEC007841, 0xEE001C1F, 0x13041B90, 0x0A000000};
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, packet[i]);
    assert(scheduled == 1 && scheduled_delay == 64);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + sizeof(words));
    assert(rp_u32(c, RP_GPU_ADDRESS(c, copy_source)) == packet[1]);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, copy_destination)) == packet[2]);
    for (unsigned i = 0; i < 5; ++i) assert(rp_u32(c, out + i * 4) == words[i]);
    for (unsigned i = 0; i < 3; ++i) assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags)));
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[3].cache_flags)) == 0xAB);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, previous_field)) == 1);

    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    rp_w32(c, RP_GPU_ADDRESS(c, copy_cost_shift), 0);
    const uint32_t row[] = {0x80000000, 0x000A000A, 0x000A000C, 0x00010008};
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, row[i]);
    assert(scheduled_delay == 8 && rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + 40);
    assert(rp_u32(c, out + 4) == 0xEC0D0000);
    assert(rp_u32(c, out + 20) == 0xEB0D0000);
    assert(rp_u32(c, out + 24) == 0xEC00280C);
    assert((rp_cd_u16(c, RP_GPU_ADDRESS(c, draw_mode)) & 0xC000) == 0xC000);

    const unsigned before = scheduled;
    const uint32_t same[] = {0x80000000, 0x00010001, 0x00010001, 0};
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, same[i]);
    assert(scheduled == before && rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + 40);
    rp_w32(c, RP_GPU_ADDRESS(c, copy_cost_shift), (uint32_t)-2);
    const uint32_t zero_width[] = {0x80000000, 0x00140000, 0x00150001, 0x00010000};
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, zero_width[i]);
    assert(scheduled_delay == 4096); /* Zero width encodes 1024, not an empty copy. */
    rp_w32(c, RP_GPU_ADDRESS(c, status), 0x800);
    if (setjmp(c->stop) == 0) {
        for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, row[i]);
        assert(!"CPU-after-GE copy was silently accepted");
    }
    assert(strcmp(c->stop_kind, "GPU_copy_CPU_sync_path_not_reconstructed") == 0);
    dma_fixture = false;
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

    /* Three writes stay buffered; the last completes a flat triangle. */
    rp_pops_gpu_write(c, 0x1810, 0x200000FF);
    rp_pops_gpu_write(c, 0x1810, 0x00010001);
    rp_pops_gpu_write(c, 0x1810, 0x00020002);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)) == 3);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x14000000) == 0);
    const uint32_t triangle_body = rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) + 8;
    memset(rp_memory(c, triangle_body, sizeof(rp_gpu_flat_ge_layout)), 0xA5, sizeof(rp_gpu_flat_ge_layout));
    dma_fixture = true; scheduled = 0;
    rp_pops_gpu_write(c, 0x1810, 0x00030003);
    dma_fixture = false;
    assert(scheduled == 1 && scheduled_delay == 30);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == triangle_body + sizeof(rp_gpu_flat_ge_layout));
    assert(rp_u32(c, triangle_body) == 0x14000000);
    assert(rp_u32(c, triangle_body + 4) == 0x550000FF);
    assert(rp_u32(c, triangle_body + 8) == UINT32_C(0x51B7F800) - triangle_body);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(triangle_body, rp_gpu_flat_ge_layout, vertices[1].x)) == 2);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(triangle_body, rp_gpu_flat_ge_layout, vertices[2].y)) == 3);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(triangle_body, rp_gpu_flat_ge_layout, vertices[0].z)) == 0xA5A5);
    scheduled = 0;
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

    /* Two linked DMA nodes feed the same GE consumer directly from guest RAM.
     * The GP0 port assembly buffer must not be repurposed as a DMA buffer. */
    c->regions[2] = (rp_region){0x09800000, 0x1000, calloc(1, 0x1000)};
    assert(c->regions[2].bytes);
    reset_status(c);
    rp_core_set_downcount(c, 1000);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00000);
    rp_w32(c, RP_GPU_ADDRESS(c, packet_words[0]), 0xA5A5A5A5);
    rp_w32(c, 0x09800100, 0x02000200);
    rp_w32(c, 0x09800104, 0xE3000001);
    rp_w32(c, 0x09800108, 0xE4004002);
    rp_w32(c, 0x09800200, 0x01FFFFFF);
    rp_w32(c, 0x09800204, 0xE6000001);
    dma_fixture = true;
    assert(rp_pops_gpu_dma_transfer(c, 0x100, 0, 0x01000401) == 1);
    assert(scheduled == 1 && scheduled_delay == 1);
    assert(scheduled_event == RP_GPU_ADDRESS(c, ready_event));
    assert(delayed_words == 17 && delay_horizon == 17);
    assert(rp_core_downcount(c) == 983);
    assert(rp_u32(c, 0x49A00000) == 0xD4000001);
    assert(rp_u32(c, 0x49A00004) == 0xD5004002);
    assert(rp_u32(c, 0x49A00008) == 0x13041B91);
    assert(rp_u32(c, 0x49A0000C) == 0x0A000010);
    assert(c->ge_stalled_list == 0x49A00010);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x14000000) == 0x10000000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, packet_words[0])) == 0xA5A5A5A5);

    /* A self-linked empty node yields a tagged continuation, not completion. */
    rp_w32(c, 0x09800300, 0x00000300);
    assert(rp_pops_gpu_dma_transfer(c, 0x300, 0, 0x01000401) == 0x80000300);
    assert(scheduled == 2 && scheduled_event == RP_DMA_ADDRESS(c, channels[2]));
    assert(rp_u32(c, RP_DMA_CHANNEL(c, 2, event.callback)) == 0x8CAC);
    assert(scheduled_delay == 36025 && delayed_words == 4);
    assert(rp_core_downcount(c) == 983);

    /* The three-word fill emits a temporary scissor and restores it. Cache
     * groups follow the same 3+3+3+3+3+1 layout as the original initializer. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00000);
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned column = i & 15;
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset),
               (uint8_t)(column == 15 ? 3 : column % 3));
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags), 0xA5);
    }
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_start[0]), 3);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_start[1]), 4);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[0]), 99);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[1]), 79);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[0]), 640);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, display_size[1]), 480);
    const unsigned before_fill = scheduled;
    rp_pops_gpu_write(c, 0x1810, 0x02123456);
    rp_pops_gpu_write(c, 0x1810, (10u << 16) | 19);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A00000);
    rp_pops_gpu_write(c, 0x1810, (8u << 16) | 32);
    const uint32_t fill_words[] = {0xD4002810, 0xD500442F, 0x55103050,
        0x13041B90, 0x0A000040, 0xD4001003, 0xD5013C63};
    for (unsigned i = 0; i < 7; ++i)
        assert(rp_u32(c, 0x49A00000 + i * 4) == fill_words[i]);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A0001C);
    assert(scheduled == before_fill + 1 && scheduled_delay == 16);
    for (unsigned i = 0; i < 32; ++i)
        assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags)) == (i < 3 ? 0 : 0xA5));
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, previous_field)) == 1);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)));
    rp_pops_gpu_write(c, 0x1810, 0x02000000);
    rp_pops_gpu_write(c, 0x1810, 0);
    rp_pops_gpu_write(c, 0x1810, 4u << 16); /* Zero width is a consumed no-op. */
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A0001C);
    assert(scheduled == before_fill + 1);

    /* A 20x20 quad: 30 + (800 double-area units / 2) / 2 = 230.
     * Its bounds invalidate a cache group and its selected texture tag. */
    reset_status(c);
    const uint32_t quad_start = 0x49A00180, quad_body = quad_start + 8;
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), quad_start);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_mode), 0x4001);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[0]), 300);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[1]), 200);
    rp_w8(c, RP_GPU_ADDRESS(c, draw_area_exceeds_display), 1);
    rp_w8(c, RP_GPU_ADDRESS(c, draw_area_intersects_display), 1);
    for (unsigned i = 0; i < 32; ++i) {
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags), 0xAB);
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset), (uint8_t)(i % 16 == 15 ? 3 : (i % 16) % 3));
    }
    c->regions[0].bytes[0xD5338 + 10] = 4;
    memset(rp_memory(c, quad_body, sizeof(rp_gpu_flat_ge_layout)), 0xA5, sizeof(rp_gpu_flat_ge_layout));
    dma_fixture = true; scheduled = 0;
    const uint32_t quad[] = {0x28112233, 0x000A0041, 0x000A0055, 0x001E0041, 0x001E0055};
    for (unsigned i = 0; i < 5; ++i) rp_pops_gpu_write(c, 0x1810, quad[i]);
    assert(scheduled == 1 && scheduled_delay == 230);
    assert(rp_u32(c, quad_start) == 0x13041B90);
    assert(rp_u32(c, quad_start + 4) == 0x0A000080);
    assert(rp_u32(c, quad_body + 4) == 0x55112233);
    assert(rp_u32(c, quad_body + 8) == UINT32_C(0x51B7F900) - quad_body);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(quad_body, rp_gpu_flat_ge_layout, vertices[3].x)) == 85);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(quad_body, rp_gpu_flat_ge_layout, vertices[3].y)) == 30);
    for (unsigned i = 0; i < 3; ++i) assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags)));
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[3].cache_flags)) == 0xAB);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, draw_mode)) == 0x8001);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, previous_field)) == 1);

    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00200);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_mode), 0);
    for (unsigned axis = 0; axis < 2; ++axis) {
        rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_start[axis]), (uint16_t)-20);
        rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[axis]), 100);
    }
    const uint32_t negative[] = {0x200000FF, 0x07FD07FB, 0x07FD0005, 0x000707FB};
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, negative[i]);
    assert(scheduled_delay == 55);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(0x49A00200, rp_gpu_flat_ge_layout, vertices[0].x)) == (uint16_t)-5);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(0x49A00200, rp_gpu_flat_ge_layout, vertices[0].y)) == (uint16_t)-3);
    dma_fixture = false;
    if (setjmp(c->stop) == 0) {
        rp_pops_gpu_write(c, 0x1810, 0x300000FF);
        assert(!"Gouraud polygon was silently accepted");
    }
    assert(strcmp(c->stop_kind, "GPU_primitive_packet_not_reconstructed") == 0);

    check_vram_copy(c);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes);
    free(c->regions[2].bytes); free(c);
    puts("GPU: state/fill/DMA, flat polygons, VRAM-copy GE/staging and cache/cost contracts passed; rendering pending.");
    return 0;
}
