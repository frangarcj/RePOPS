#ifndef REPOPS_GPU_H
#define REPOPS_GPU_H
#include "pops_cdrom.h"

typedef struct {
    uint32_t storage_address;
    uint8_t cache_flags;
    int8_t group_offset;
    uint16_t group_x_origin;
} rp_gpu_texture_cache_entry;
typedef struct { uint32_t command, origin, extent; } rp_gpu_fill_packet_layout;
typedef struct { uint32_t command, source, destination, extent; } rp_gpu_copy_packet_layout;
typedef struct { uint32_t command, destination, extent; } rp_gpu_upload_packet_layout;
typedef struct { uint32_t command, source, extent; } rp_gpu_readback_packet_layout;
typedef struct { uint32_t command, positions[4]; } rp_gpu_flat_packet_layout;
typedef struct { int16_t x, y, z; } rp_gpu_position_layout;
typedef struct {
    uint32_t offset_command, color_command, template_jump;
    rp_gpu_position_layout vertices[4];
} rp_gpu_flat_ge_layout;
typedef struct { uint32_t command, position, extent; } rp_gpu_rectangle_packet_layout;
typedef struct {
    uint32_t command, position;
    uint8_t u, v;
    uint16_t palette;
    uint32_t extent;
} rp_gpu_textured_rectangle_packet_layout;
typedef struct { uint16_t u, v; int16_t x, y, z; } rp_gpu_textured_vertex_layout;
typedef struct {
    uint32_t palette_address, palette_load, offset_command, scale_u, scale_v;
    uint32_t color_command, draw_call, mode_jump;
    rp_gpu_textured_vertex_layout vertices[2];
    uint32_t restore_u, restore_v;
} rp_gpu_textured_rectangle_ge_layout;
typedef struct {
    uint32_t offset_command, color_command, template_jump;
    rp_gpu_position_layout vertices[2];
} rp_gpu_rectangle_ge_layout;

/* Selected core GPU fields, recovered from +0x12FBC and their producers.
 * This is a wire view: use named addresses, never cast the guest backing. */
typedef struct {
    uint8_t earlier_000[0x6D4];
    uint32_t texture_color_word_mask;
    uint8_t unknown_6d8[0x6E8 - 0x6D8];
    uint32_t dma_cost_scaling;
    uint8_t unknown_6ec[0x710 - 0x6EC];
    uint32_t data_read_cycle_cost;
    int16_t texture_offset_word_bias[2];
    int32_t copy_cost_shift;
    uint8_t unknown_71c[0x3400 - 0x71C];
    rp_gpu_texture_cache_entry texture_cache[32];
    union {
        uint32_t packet_words[48];
        struct { uint8_t packet_prefix[0xBC]; uint32_t data_read_latch; };
    };
    uint32_t frame_counter, frame_cycle_origin;
    uint32_t previous_display_source, list_id;
    rp_guest_event_layout earlier_event;
    uint32_t display_rate_remaining;
    rp_guest_event_layout frame_event;
    uint32_t remaining_frame_delay;
    rp_guest_event_layout ready_event;
    uint32_t audio_sample_origin, audio_cycle_origin;
    uint16_t display_origin[2], horizontal_range[2], vertical_range[2], display_size[2];
    int16_t draw_area_start[2], draw_area_end[2];
    uint16_t drawing_offset[2];
    uint32_t list_cursor, status;
    uint32_t status_poll_previous_cycles, status_poll_last_cycles;
    union {
        struct {
            uint16_t transfer_origin[2];
            union { uint16_t transfer_size[2], upload_end[2]; };
        };
        struct { uint16_t copy_source[2], copy_destination[2]; };
    };
    uint16_t transfer_cursor[2];
    uint32_t transfer_read_latch, texture_window;
    uint8_t texture_window_offset[2], texture_window_size[2];
    uint16_t draw_mode;
    uint8_t texture_depth, command_mode, ge_transfer_pending, read_selector;
    uint8_t draw_area_exceeds_display, draw_area_intersects_display;
    uint8_t draw_mode_gate, display_mode_gate;
    uint8_t interlaced, display_dirty, previous_field, refresh_on_ready, frame_phase;
    uint8_t display_choice, external_output, external_field_mode;
    uint8_t display_initialized, frame_refreshed;
    union {
        uint32_t display_mode;
        struct { uint8_t display_mode_bytes[3]; uint8_t packet_extra_words; };
    };
    uint8_t packet_word_count;
    int32_t pal_frame_phase;
} rp_core_gpu_layout;

#define RP_GPU_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_gpu_layout, member)
enum {
    RP_GPU_READ_START = 16, RP_GPU_READ_PIXELS = 17, RP_GPU_READ_FINISHED = 255
};
uint32_t rp_pops_gpu_read(rp_context *, uint32_t address, uint32_t width);
void rp_pops_gpu_read_data(rp_context *, uint32_t destination, uint32_t bytes);
uint32_t rp_pops_gpu_dma_readback(rp_context *, uint32_t address, uint32_t bytes);
void rp_pops_gpu_write(rp_context *, uint32_t address, uint32_t word);
uint32_t rp_pops_gpu_dma_transfer(rp_context *, uint32_t address, uint32_t bytes, uint32_t control);
void rp_pops_gpu_submit_pending_list(rp_context *);
/* Existing headless adapter for bounded state lists, not a POPSMAN body. */
uint32_t rp_ge_capture_state_list(rp_context *, uint32_t address, int module_relative);
/* These backend barriers may return only after GE writes are visible. The
 * current headless implementation stops; isolated tests supply known pixels. */
uint32_t rp_ge_readback_restart_list(rp_context *, uint32_t old_list);
uint32_t rp_ge_readback_barrier(rp_context *, uint32_t old_list, uint32_t continuation);
uint32_t rp_popsman_ge_finish_host(rp_context *, uint32_t old_list,
                                  uint32_t continuation, bool require_pixels);
enum { RP_GPU_DISPLAY_TRANSITION_ADDRESS = 0x49CBD4 };
static inline uint32_t rp_gpu_vram_pixel(uint32_t x, uint32_t y)
{ return UINT32_C(0x44000000) | ((y & 511) << 11) | ((x & 1023) << 1); }

_Static_assert(offsetof(rp_core_gpu_layout, data_read_latch) == 0x35BC, "GPU data latch");
_Static_assert(sizeof(rp_gpu_texture_cache_entry) == 8, "GPU texture cache stride");
_Static_assert(sizeof(rp_gpu_fill_packet_layout) == 12, "GPU fill packet bytes");
_Static_assert(sizeof(rp_gpu_copy_packet_layout) == 16, "GPU copy packet bytes");
_Static_assert(sizeof(rp_gpu_upload_packet_layout) == 12, "GPU upload header bytes");
_Static_assert(sizeof(rp_gpu_readback_packet_layout) == 12, "GPU readback header bytes");
_Static_assert(offsetof(rp_core_gpu_layout, copy_cost_shift) == 0x718, "GPU copy cycle shift");
_Static_assert(offsetof(rp_core_gpu_layout, copy_destination) == 0x3640, "GPU copy destination alias");
_Static_assert(offsetof(rp_core_gpu_layout, upload_end) == 0x3640, "GPU CPU-upload end alias");
_Static_assert(sizeof(rp_gpu_flat_packet_layout) == 20, "flat quad packet");
_Static_assert(sizeof(rp_gpu_flat_ge_layout) == 36, "flat GE body");
_Static_assert(sizeof(rp_gpu_rectangle_ge_layout) == 24, "rectangle GE body");
_Static_assert(sizeof(rp_gpu_textured_vertex_layout) == 10, "textured vertex stride");
_Static_assert(sizeof(rp_gpu_textured_rectangle_packet_layout) == 16, "textured rectangle packet");
_Static_assert(sizeof(rp_gpu_textured_rectangle_ge_layout) == 60, "textured rectangle GE body");
_Static_assert(offsetof(rp_gpu_textured_rectangle_ge_layout, vertices) == 32, "rectangle GE vertices");
_Static_assert(offsetof(rp_core_gpu_layout, texture_color_word_mask) == 0x6D4, "texture color mask");
_Static_assert(offsetof(rp_core_gpu_layout, texture_offset_word_bias) == 0x714, "texture offset bias");
_Static_assert(offsetof(rp_core_gpu_layout, texture_depth) == 0x3656, "texture depth selector");
_Static_assert(offsetof(rp_gpu_flat_ge_layout, vertices[1].x) == 18, "flat second vertex");
_Static_assert(offsetof(rp_core_gpu_layout, texture_cache) == 0x3400, "GPU texture cache base");
_Static_assert(offsetof(rp_core_gpu_layout, dma_cost_scaling) == 0x6E8, "GPU DMA cost scaling");
_Static_assert(offsetof(rp_core_gpu_layout, packet_words[47]) == 0x35BC, "GPU packet/latch alias");
_Static_assert(offsetof(rp_core_gpu_layout, ready_event) == 0x35F8, "GPU ready event");
_Static_assert(offsetof(rp_core_gpu_layout, status) == 0x3630, "GPU status");
_Static_assert(offsetof(rp_core_gpu_layout, draw_mode) == 0x3654, "GPU draw mode");
_Static_assert(offsetof(rp_core_gpu_layout, read_selector) == 0x3659, "GPU read selector");
_Static_assert(offsetof(rp_core_gpu_layout, frame_phase) == 0x3662, "GPU frame phase");
_Static_assert(offsetof(rp_core_gpu_layout, refresh_on_ready) == 0x3661, "GPU ready refresh flag");
_Static_assert(offsetof(rp_core_gpu_layout, display_mode) == 0x3668, "GPU display mode");
_Static_assert(offsetof(rp_core_gpu_layout, pal_frame_phase) == 0x3670, "PAL frame phase");
_Static_assert(offsetof(rp_core_gpu_layout, packet_extra_words) == 0x366B, "GPU packet extra words");
_Static_assert(offsetof(rp_core_gpu_layout, packet_word_count) == 0x366C, "GPU packet count");
#endif
