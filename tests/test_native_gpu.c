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
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_gpu_read(c, 0x1810, 2);
        assert(!"Unimplemented GPU data path returned");
    }
    assert(strcmp(c->stop_kind, "GPU_data_read_not_reconstructed") == 0);
    assert(c->stop_address == 0x130BC && rp_core_downcount(c) == 4993);
    c->regions[0] = (rp_region){0, 0xD5400, calloc(1, 0xD5400)};
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
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("GPU: status, GP0 framing, named draw state and exact state-list words passed; rasterization pending.");
    return 0;
}
