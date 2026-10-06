#ifndef REPOPS_DISPLAY_H
#define REPOPS_DISPLAY_H
#include "pops_gpu.h"

/* Wire layouts from +0x11BF4..+0x122F8. They describe stored tables and
 * vertices, not a replacement host renderer. Use the endian-aware accessors. */
typedef struct {
    uint8_t horizontal_step[5];
    int8_t primitive_vertex_count;
    uint8_t horizontal_bias, width_units16, width_rounding;
} rp_display_resolution_layout;
typedef struct {
    int8_t x_offset, y_top_offset, y_bottom_offset, x_scale_q8;
    int8_t y_scale_delta[2];
} rp_display_fit_layout;
typedef struct { uint16_t u, v; int16_t x, y, z; } rp_display_vertex_layout;
typedef struct { rp_display_vertex_layout first, second; } rp_display_sprite_layout;
typedef struct {
    uint8_t earlier_000[0x6B4];
    uint16_t vertical_clip_limit;
    uint8_t unknown_6b6[2];
    int32_t crop_start[2];
    uint8_t unknown_6c0[0x6DC - 0x6C0];
    uint32_t frame_rate_ratio;
} rp_core_display_config_layout;
typedef struct {
    uint32_t choice;
    uint8_t unknown_04[16];
    int32_t background_level;
} rp_display_settings_layout;

enum {
    RP_DISPLAY_RESOLUTION_TABLE = 0xFED00,
    RP_DISPLAY_FIT_TABLE = 0xFED30,
    RP_DISPLAY_SETTINGS_BASE = 0x163220,
    RP_DISPLAY_SPRITE_BASE = 0x441BBE00
};
#define RP_DISPLAY_CONFIG(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_display_config_layout, member)
#define RP_DISPLAY_SETTINGS(member) RP_FIELD_ADDRESS(RP_DISPLAY_SETTINGS_BASE, rp_display_settings_layout, member)
#define RP_DISPLAY_SPRITE(index, member) RP_FIELD_ADDRESS( \
    RP_DISPLAY_SPRITE_BASE + (uint32_t)(index) * sizeof(rp_display_sprite_layout), \
    rp_display_sprite_layout, member)

uint32_t rp_pops_display_active_lists(rp_context *, uint32_t cursor);
_Static_assert(sizeof(rp_display_resolution_layout) == 9, "resolution row");
_Static_assert(sizeof(rp_display_fit_layout) == 6, "display fit row");
_Static_assert(sizeof(rp_display_sprite_layout) == 20, "GE sprite pair");
_Static_assert(offsetof(rp_display_sprite_layout, second.u) == 10, "second vertex");
_Static_assert(offsetof(rp_core_display_config_layout, frame_rate_ratio) == 0x6DC, "display pacing");
_Static_assert(offsetof(rp_display_settings_layout, background_level) == 0x14, "background setting");
#endif
