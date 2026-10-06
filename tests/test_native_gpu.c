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
static unsigned ready_calls;
void rp_pops_graphics_event(rp_context *c, uint32_t callback)
{
    assert(dma_fixture && callback == 0x125F0);
    ++ready_calls;
    rp_w32(c, RP_GPU_ADDRESS(c, status), rp_u32(c, RP_GPU_ADDRESS(c, status)) | 0x14000000);
    rp_pops_gpu_submit_pending_list(c);
}
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

static void check_cpu_upload(rp_context *c)
{
    reset_status(c);
    const uint32_t out = 0x49A00800;
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    for (unsigned i = 0; i < 32; ++i)
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset),
               (uint8_t)(i % 16 == 15 ? 3 : (i % 16) % 3));
    c->regions[0].bytes[0xD5338 + 40] = 2;
    dma_fixture = true; scheduled = 0;
    const uint32_t header[] = {0xA0000000, 0x00030008, 0x00010010};
    for (unsigned i = 0; i < 3; ++i) rp_pops_gpu_write(c, 0x1810, header[i]);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)) == 9);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_size)) == header[2]);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out && scheduled == 0);
    /* Command-looking pixel words must stay pixel data, not become GP0 ops. */
    for (unsigned i = 0; i < 7; ++i) rp_pops_gpu_write(c, 0x1810, 0xE100ABCD);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out);
    rp_pops_gpu_write(c, 0x1810, 0xA0001234);
    const uint32_t direct[] = {0xB2013500, 0xB3000010, 0xEB000000,
        0xEC000C08, 0xEE00000F, 0x13041B90, 0x0A000008};
    for (unsigned i = 0; i < 7; ++i) assert(rp_u32(c, out + i * 4) == direct[i]);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)));
    assert(rp_u32(c, out + sizeof(direct)) == 0x0F000000);
    assert(rp_u32(c, out + sizeof(direct) + 4) == 0x0C000000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + sizeof(direct) + 4);
    assert(scheduled == 1 && scheduled_delay == 8);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, packet_words[7])) == 0xA0001234);

    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    const uint32_t odd[] = {0xA0000000, 0x00030008, 0x00020003,
                            0x22221111, 0x44443333, 0x66665555};
    for (unsigned i = 0; i < 6; ++i) rp_pops_gpu_write(c, 0x1810, odd[i]);
    const uint32_t rows[] = {0xB4000000, 0xB5040400, 0xB3000400,
        0xEE000002, 0xB2013500, 0xEC000C08, 0xEB000000, 0xEA000000,
        0xEC001008, 0xEB000003, 0xEA000000};
    for (unsigned i = 0; i < 11; ++i) assert(rp_u32(c, out + i * 4) == rows[i]);
    assert(scheduled_delay == 3);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + sizeof(rows) + 4);

    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    rp_pops_gpu_write(c, 0x1810, 0xA0000000);
    rp_pops_gpu_write(c, 0x1810, 0x00030008);
    rp_pops_gpu_write(c, 0x1810, 0x00010078); /* Port buffer holds 96 of 120 pixels. */
    for (unsigned i = 0; i < 48; ++i) rp_pops_gpu_write(c, 0x1810, 0x22221111);
    assert(scheduled_delay == 48);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)) == 9);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, transfer_origin[0])) == 104);
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, transfer_size[0])) == 24);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)));
    assert(rp_u32(c, out + 32) == 0x0F000000);
    const uint32_t continuation = out + 36;
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == continuation);
    for (unsigned i = 0; i < 12; ++i) rp_pops_gpu_write(c, 0x1810, 0x44443333);
    assert(scheduled_delay == 12 && !rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
    assert(rp_u32(c, continuation + 12) == 0xEC000C68);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + 68);

    /* +0x12624 then closes the pending list exactly where the provider call
     * sees it, advances the stall by one word and publishes a replacement id. */
    const uint32_t pending_cursor = rp_u32(c, RP_GPU_ADDRESS(c, list_cursor));
    const uint32_t old_id = 0x1234;
    rp_w32(c, RP_GPU_ADDRESS(c, list_id), old_id);
    rp_w8(c, RP_GPU_ADDRESS(c, ge_transfer_pending), 1);
    c->next_id = 0x40;
    rp_pops_gpu_submit_pending_list(c);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)));
    assert(rp_u32(c, pending_cursor) == 0x0F000000);
    assert(rp_u32(c, pending_cursor + 4) == 0x0C000000);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == pending_cursor + 4);
    assert(c->ge_stalled_list == pending_cursor + 4);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_id)) == 0x41);

    /* DMA block mode uses the same live mode-9 receiver rather than parsing
     * pixel words as linked-list headers or GP0 commands. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
    scheduled = 0;
    const uint32_t dma_header[] = {0xA0000000, 0x00030008, 0x00010010};
    for (unsigned i = 0; i < 3; ++i) rp_pops_gpu_write(c, 0x1810, dma_header[i]);
    for (unsigned i = 0; i < 8; ++i) rp_w32(c, 0x09800600 + i * 4, 0xE1000000 | i);
    assert(rp_pops_gpu_dma_transfer(c, 0x600, 32, 0x01000201) == 32);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)) == 1);
    assert(scheduled == 1 && scheduled_event == RP_GPU_ADDRESS(c, ready_event));
    assert(scheduled_delay == 8);
    assert(c->ge_stalled_list == rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)));
    assert(rp_u32(c, out) == 0xB2800600);
    assert(rp_u32(c, out + 4) == 0xB3090010);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 0x10);
    for (unsigned i = 0; i < 3; ++i) rp_pops_gpu_write(c, 0x1810, dma_header[i]);
    assert(rp_pops_gpu_dma_transfer(c, 0x600, 32, 0x01000201) == 1);
    assert(ready_calls == 1 && scheduled_delay == 1);
    assert((rp_u32(c, RP_GPU_ADDRESS(c, status)) & 0x14000000) == 0x14000000);
    dma_fixture = false;
}

static void check_mixed_upload(rp_context *c)
{
    c->regions[3] = (rp_region){0x04000000, 0x100000, malloc(0x100000)};
    assert(c->regions[3].bytes);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected mixed-upload boundary: %s\n", c->stop_kind);
        abort();
    }
    const uint32_t out = 0x49A00800, source = 0x09800600;
    for (unsigned test = 0; test < 3; ++test) {
        reset_status(c);
        memset(c->regions[3].bytes, 0xA5, c->regions[3].size);
        rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), out);
        for (unsigned i = 0; i < 32; ++i)
            rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset),
                      (uint8_t)(i % 16 == 15 ? 3 : (i % 16) % 3));
        const unsigned width = test == 0 ? 16 : test == 1 ? 50 : 3;
        const unsigned height = test == 0 ? 1 : test == 1 ? 4 : 3;
        const unsigned x = test == 2 ? 1023 : 10, y = test == 2 ? 511 : 20;
        const unsigned prefix_words = test == 2 ? 0 : 2;
        const unsigned words = (width * height + 1) / 2;
        if (test == 2) rp_w32(c, RP_GPU_ADDRESS(c, status), 0x800);
        dma_fixture = true;
        scheduled = 0;
        rp_pops_gpu_write(c, 0x1810, 0xA0000000);
        rp_pops_gpu_write(c, 0x1810, x | (y << 16));
        rp_pops_gpu_write(c, 0x1810, width | (height << 16));
        for (unsigned i = 0; i < words; ++i) {
            const uint32_t pair = (0x100 + i * 2) | ((0x101 + i * 2) << 16);
            if (i < prefix_words) rp_pops_gpu_write(c, 0x1810, pair);
            else rp_w32(c, source + (i - prefix_words) * 4, pair);
        }
        assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)) == prefix_words);
        const uint32_t bytes = (words - prefix_words) * 4;
        assert(rp_pops_gpu_dma_transfer(c, 0x600, bytes, 0x01000201) == bytes);
        assert(scheduled_delay == bytes / 4);
        assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
        assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, packet_word_count)));
        if (test == 0) {
            /* Small prefix + block is one GE upload from the port buffer. */
            assert(rp_u32(c, out) == 0xB2013500);
            assert(rp_u32(c, RP_GPU_ADDRESS(c, packet_words[0])) == 0x01010100);
            assert(rp_u32(c, RP_GPU_ADDRESS(c, packet_words[7])) == 0x010F010E);
        } else {
            /* Large partial multi-row and mask/wrap use original CPU stores,
             * including low/high ordering across rows and odd tail padding. */
            assert(rp_u32(c, out) == 0x0F000000);
            assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + 4);
            const uint16_t mask = test == 2 ? 0x8000 : 0;
            for (unsigned pixel = 0; pixel < width * height; ++pixel)
                assert(rp_cd_u16(c, rp_gpu_vram_pixel(x + pixel % width, y + pixel / width)) ==
                       ((0x100 + pixel) | mask));
            assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, transfer_cursor[1])) == y + height);
            assert(rp_cd_u16(c, rp_gpu_vram_pixel(x + width, y + height - 1)) == 0xA5A5);
            assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, upload_end[0])) == x + width);
        }
    }
    dma_fixture = false;
    free(c->regions[3].bytes);
    c->regions[3] = (rp_region){0};
}

static void prepare_rectangle_fixture(rp_context *c, uint16_t draw_mode)
{
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00800);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_mode), draw_mode);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[0]), 320);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_area_end[1]), 240);
    rp_w8(c, RP_GPU_ADDRESS(c, draw_area_intersects_display), 1);
    rp_w8(c, RP_GPU_ADDRESS(c, texture_depth), 0xFF);
    rp_w8(c, RP_GPU_ADDRESS(c, texture_window_size[0]), 32);
    rp_w8(c, RP_GPU_ADDRESS(c, texture_window_size[1]), 32);
    rp_w32(c, RP_GPU_ADDRESS(c, texture_color_word_mask), UINT32_MAX);
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned column = i & 15, plane = column == 15 ? 3 : column % 3;
        const unsigned group = column - plane;
        const uint32_t storage = (i < 16 ? 0x04360000u : 0x04100000u) + group / 3 * 0x20000;
        rp_w32(c, RP_GPU_ADDRESS(c, texture_cache[i].storage_address), storage + plane * 128);
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[i].group_offset), (uint8_t)plane);
        rp_cd_w16(c, RP_GPU_ADDRESS(c, texture_cache[i].group_x_origin), (uint16_t)(plane ? 0 : group * 64));
    }
    c->regions[0].bytes[0xD5338 + 24] = 2;
    c->regions[0].bytes[0xD5338 + 25] = 3;
    c->regions[0].bytes[0xD5338 + 26] = 1;
    c->regions[0].bytes[0xD5338 + 29] = 2;
    dma_fixture = true;
    scheduled = 0;
}

static void check_rectangles(rp_context *c)
{
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected rectangle boundary: %s\n", c->stop_kind);
        abort();
    }
    const uint32_t out = 0x49A00800;
    const uint32_t packet[] = {0x64808080, 0x001E0014, 0x12340305, 0x00080010};
    prepare_rectangle_fixture(c, 0xC20A);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, texture_offset_word_bias[0]), 16);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, texture_offset_word_bias[1]), (uint16_t)-32);
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, packet[i]);
    const uint32_t prefix[] = {0x13041B90, 0x0A000080, 0xEB000240,
        0xB43C0000, 0xB5040100, 0x0A0000C0, 0xC3000004,
        0xA8040400, 0xA03C0080, 0xB8000808, 0x4ABF7F9D, 0x4BBF7F6E,
        0xCB000000, 0xCC000000};
    for (unsigned i = 0; i < sizeof(prefix) / sizeof(prefix[0]); ++i)
        assert(rp_u32(c, out + i * 4) == prefix[i]);
    const uint32_t record = out + sizeof(prefix), body = record + 8;
    const uint32_t words[] = {0xB0024680, 0xC4000010, 0x14000000,
        0x48430000, 0x49430000, 0x55808080, UINT32_C(0x53B7FD80) - body,
        UINT32_C(0x51B7FC40) - body, 0x00030005, 0x001E0014, 0x0015000B,
        0x0024000B, 0x00000026, 0x483F0000, 0x493F0000};
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        assert(rp_u32(c, record + i * 4) == words[i]);
    assert(scheduled_delay == 222);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == out + sizeof(prefix) + sizeof(words));
    assert(rp_cd_u16(c, RP_GPU_ADDRESS(c, draw_mode)) == 0x20A);
    for (unsigned i = 0; i < 32; ++i)
        assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[i].cache_flags)) == (i >= 9 && i <= 11 ? 4 : 0));
    const uint32_t warm = rp_u32(c, RP_GPU_ADDRESS(c, list_cursor));
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, packet[i]);
    assert(scheduled_delay == 94 && rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == warm + 60);
    assert(rp_u32(c, warm) == 0xB0024680);

    prepare_rectangle_fixture(c, 0xC00A);
    rp_w8(c, RP_GPU_ADDRESS(c, texture_window_offset[0]), 1);
    for (unsigned i = 0; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, packet[i]);
    assert(rp_u32(c, out + 8) == 0xEE03FC07);
    assert(rp_u32(c, out + 12) == 0xEB000282);
    assert(rp_u32(c, out + 20) == 0xB5040008);
    assert(rp_u32(c, out + 32) == 0xA8040020);

    prepare_rectangle_fixture(c, 0xC10A);
    rp_pops_gpu_write(c, 0x1810, 0x65800000); /* Raw texture forces neutral color. */
    for (unsigned i = 1; i < 4; ++i) rp_pops_gpu_write(c, 0x1810, packet[i]);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_depth)) == 2);
    assert(rp_u32(c, out + 8) == 0xC3000001);
    assert(rp_u32(c, out + 16) == 0xA0000500);
    assert(rp_u32(c, out + 40 + 20) == 0x55808080);

    prepare_rectangle_fixture(c, 0xC00A);
    memset(rp_memory(c, out + 8, sizeof(rp_gpu_rectangle_ge_layout)), 0xA5, sizeof(rp_gpu_rectangle_ge_layout));
    rp_pops_gpu_write(c, 0x1810, 0x60112233);
    rp_pops_gpu_write(c, 0x1810, packet[1]);
    rp_pops_gpu_write(c, 0x1810, packet[3]);
    assert(scheduled_delay == 94 && rp_u32(c, out + 12) == 0x55112233);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(out + 8, rp_gpu_rectangle_ge_layout, vertices[0].z)) == 0xA5A5);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(out + 8, rp_gpu_rectangle_ge_layout, vertices[1].x)) == 36);
    assert(rp_cd_u16(c, RP_FIELD_ADDRESS(out + 8, rp_gpu_rectangle_ge_layout, vertices[1].y)) == 38);

    prepare_rectangle_fixture(c, 0xC080);
    rp_pops_gpu_write(c, 0x1810, 0x74808080); /* Fixed 8x8, no size word. */
    rp_pops_gpu_write(c, 0x1810, packet[1]);
    rp_pops_gpu_write(c, 0x1810, packet[2]);
    assert(scheduled_delay == 126);
    assert(rp_u32(c, out + 24) == 0xC3000005);
    assert(rp_u32(c, out + 28) == 0xA8040200);
    dma_fixture = false;
}

static void check_readback_header(rp_context *c)
{
    reset_status(c);
    c->regions[0].bytes[0xD5338 + 48] = 2;
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00800);
    rp_w32(c, RP_GPU_ADDRESS(c, transfer_cursor), 0xABCD0123);
    rp_w32(c, RP_GPU_ADDRESS(c, transfer_read_latch), 0x1234ABCD);
    const unsigned before = scheduled;
    rp_pops_gpu_write(c, 0x1F801810, 0xC0000000);
    rp_pops_gpu_write(c, 0x1F801810, 0xFFFFFC08);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == 0);
    rp_pops_gpu_write(c, 0x1F801810, 0);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_origin)) == 0x01FF0008);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_size)) == 0x02000400);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == 16);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, command_mode)));
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_cursor)) == 0xABCD0123);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == 0x1234ABCD);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == 0x49A00800);
    assert(scheduled == before);
    if (!setjmp(c->stop)) {
        (void)rp_pops_gpu_read(c, 0x1F801810, 2);
        assert(!"Readback returned fabricated framebuffer data");
    }
    assert(!strcmp(c->stop_kind, "GPU_VRAM_data_transfer_not_reconstructed"));
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
    check_cpu_upload(c);
    check_mixed_upload(c);
    check_rectangles(c);
    check_readback_header(c);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes);
    free(c->regions[2].bytes); free(c);
    puts("GPU: uploads, primitives and readback header/state boundary passed; rendering pending.");
    return 0;
}
