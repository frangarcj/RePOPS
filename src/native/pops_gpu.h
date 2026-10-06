#ifndef REPOPS_GPU_H
#define REPOPS_GPU_H
#include "pops_cdrom.h"

/* Selected core GPU fields, recovered from +0x12FBC and their producers.
 * This is a wire view: use named addresses, never cast the guest backing. */
typedef struct {
    uint8_t earlier_000[0x6E8];
    uint32_t dma_cost_scaling;
    uint8_t unknown_6ec[0x710 - 0x6EC];
    uint32_t data_read_cycle_cost;
    uint8_t unknown_714[0x3500 - 0x714];
    union {
        uint32_t packet_words[48];
        struct { uint8_t packet_prefix[0xBC]; uint32_t data_read_latch; };
    };
    uint32_t frame_counter, frame_cycle_origin;
    uint32_t previous_display_source, list_id;
    rp_guest_event_layout earlier_event;
    uint32_t unknown_35e0;
    rp_guest_event_layout frame_event;
    uint32_t remaining_frame_delay;
    rp_guest_event_layout ready_event;
    uint32_t audio_sample_origin, audio_cycle_origin;
    uint16_t display_origin[2], horizontal_range[2], vertical_range[2], display_size[2];
    int16_t draw_area_start[2], draw_area_end[2];
    uint16_t drawing_offset[2];
    uint32_t list_cursor, status;
    uint32_t status_poll_previous_cycles, status_poll_last_cycles;
    uint16_t transfer_origin[2], transfer_size[2], transfer_cursor[2];
    uint32_t transfer_read_latch, texture_window;
    uint8_t texture_window_offset[2], texture_window_size[2];
    uint16_t draw_mode;
    uint8_t unknown_3656, command_mode, ge_transfer_pending, read_selector;
    uint8_t draw_area_exceeds_display, draw_area_intersects_display;
    uint8_t draw_mode_gate, display_mode_gate;
    uint8_t interlaced, display_dirty, previous_field, unknown_3661, frame_phase;
    uint8_t unknown_3663[5];
    union {
        uint32_t display_mode;
        struct { uint8_t display_mode_bytes[3]; uint8_t packet_extra_words; };
    };
    uint8_t packet_word_count;
} rp_core_gpu_layout;

#define RP_GPU_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_gpu_layout, member)
uint32_t rp_pops_gpu_read(rp_context *, uint32_t address, uint32_t width);
void rp_pops_gpu_write(rp_context *, uint32_t address, uint32_t word);
uint32_t rp_pops_gpu_dma_transfer(rp_context *, uint32_t address, uint32_t bytes, uint32_t control);
/* Existing headless adapter for bounded state lists, not a POPSMAN body. */
uint32_t rp_ge_capture_state_list(rp_context *, uint32_t address, int module_relative);
enum { RP_GPU_DISPLAY_TRANSITION_ADDRESS = 0x49CBD4 };

_Static_assert(offsetof(rp_core_gpu_layout, data_read_latch) == 0x35BC, "GPU data latch");
_Static_assert(offsetof(rp_core_gpu_layout, dma_cost_scaling) == 0x6E8, "GPU DMA cost scaling");
_Static_assert(offsetof(rp_core_gpu_layout, packet_words[47]) == 0x35BC, "GPU packet/latch alias");
_Static_assert(offsetof(rp_core_gpu_layout, ready_event) == 0x35F8, "GPU ready event");
_Static_assert(offsetof(rp_core_gpu_layout, status) == 0x3630, "GPU status");
_Static_assert(offsetof(rp_core_gpu_layout, draw_mode) == 0x3654, "GPU draw mode");
_Static_assert(offsetof(rp_core_gpu_layout, read_selector) == 0x3659, "GPU read selector");
_Static_assert(offsetof(rp_core_gpu_layout, frame_phase) == 0x3662, "GPU frame phase");
_Static_assert(offsetof(rp_core_gpu_layout, display_mode) == 0x3668, "GPU display mode");
_Static_assert(offsetof(rp_core_gpu_layout, packet_extra_words) == 0x366B, "GPU packet extra words");
_Static_assert(offsetof(rp_core_gpu_layout, packet_word_count) == 0x366C, "GPU packet count");
#endif
