#ifndef REPOPS_GPU_H
#define REPOPS_GPU_H
#include "pops_state.h"

/* Selected core GPU fields, recovered from +0x12FBC and their producers.
 * This is a wire view: use named addresses, never cast the guest backing. */
typedef struct {
    uint8_t earlier_000[0x710];
    uint32_t data_read_cycle_cost;
    uint8_t unknown_714[0x35BC - 0x714];
    uint32_t data_read_latch, frame_counter, frame_cycle_origin;
    uint32_t previous_display_source, list_id;
    uint8_t events_35d0[0x3608 - 0x35D0];
    uint32_t audio_sample_origin, audio_cycle_origin;
    uint8_t display_coordinates_3610[0x10];
    int16_t draw_area_start[2], draw_area_end[2];
    uint16_t drawing_offset[2];
    uint32_t list_cursor, status;
    uint32_t status_poll_previous_cycles, status_poll_last_cycles;
    uint16_t transfer_origin[2], transfer_size[2], transfer_cursor[2];
    uint32_t transfer_read_latch, texture_window;
    uint8_t unknown_3650[4];
    uint16_t draw_mode;
    uint8_t unknown_3656[2], ge_transfer_pending, read_selector;
    uint8_t unknown_365a[4];
    uint8_t interlaced, display_dirty, previous_field, unknown_3661, frame_phase;
    uint8_t unknown_3663[5];
    uint32_t display_mode;
} rp_core_gpu_layout;

#define RP_GPU_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_gpu_layout, member)
uint32_t rp_pops_gpu_read(rp_context *, uint32_t address, uint32_t width);

_Static_assert(offsetof(rp_core_gpu_layout, data_read_latch) == 0x35BC, "GPU data latch");
_Static_assert(offsetof(rp_core_gpu_layout, status) == 0x3630, "GPU status");
_Static_assert(offsetof(rp_core_gpu_layout, draw_mode) == 0x3654, "GPU draw mode");
_Static_assert(offsetof(rp_core_gpu_layout, read_selector) == 0x3659, "GPU read selector");
_Static_assert(offsetof(rp_core_gpu_layout, frame_phase) == 0x3662, "GPU frame phase");
_Static_assert(offsetof(rp_core_gpu_layout, display_mode) == 0x3668, "GPU display mode");
#endif
