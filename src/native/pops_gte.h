#ifndef REPOPS_GTE_H
#define REPOPS_GTE_H
#include "pops_state.h"

/* State consumed by the original RTPT helpers, not a host-pointer overlay. */
typedef struct { int16_t component[3]; uint16_t padding; } rp_gte_vector_layout;
typedef struct { int16_t x, y; } rp_gte_screen_layout;
typedef struct { uint16_t value, padding; } rp_gte_depth_layout;
typedef struct {
    rp_gte_vector_layout vectors[3];
    uint8_t unknown_18[8];
    int32_t ir[4];
    rp_gte_screen_layout screen[3];
    uint32_t screen_alias;
    rp_gte_depth_layout depth[4];
    uint8_t unknown_50[16];
    int32_t mac[4];
    uint8_t unknown_70[16];
    int16_t rotation[3][3];
    uint16_t rotation_padding;
    int32_t translation[3];
    uint8_t unknown_a0[0x40];
    int32_t screen_offset[2];
    uint16_t projection_distance, unknown_ea;
    int16_t depth_cue_slope;
    uint16_t unknown_ee;
    int32_t depth_cue_intercept;
    uint8_t unknown_f4[8];
    uint32_t flags_shadow;
} rp_core_gte_layout;

#define RP_GTE_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_gte_layout, member)
enum {
    RP_GTE_NCLIP = 0x06,
    RP_GTE_NCLIP_HELPER = 0x10B34,
    RP_GTE_RTPT = 0x30,
    RP_GTE_RTPT_FLAGS_HELPER = 0x10B14,
    RP_GTE_RTPT_NO_FLAGS_HELPER = 0x10B24,
    RP_GTE_RECIPROCAL_ANCHOR = 0x09800000
};
void rp_pops_gte_rtpt(rp_context *, bool update_flags);
void rp_pops_gte_nclip(rp_context *);

_Static_assert(sizeof(rp_gte_vector_layout) == 8, "GTE input vector stride");
_Static_assert(offsetof(rp_core_gte_layout, ir) == 0x20, "GTE IR registers");
_Static_assert(offsetof(rp_core_gte_layout, screen) == 0x30, "GTE screen FIFO");
_Static_assert(offsetof(rp_core_gte_layout, depth) == 0x40, "GTE depth FIFO");
_Static_assert(offsetof(rp_core_gte_layout, mac) == 0x60, "GTE MAC registers");
_Static_assert(offsetof(rp_core_gte_layout, rotation) == 0x80, "GTE rotation matrix");
_Static_assert(offsetof(rp_core_gte_layout, translation) == 0x94, "GTE translation vector");
_Static_assert(offsetof(rp_core_gte_layout, screen_offset) == 0xE0, "GTE screen offsets");
_Static_assert(offsetof(rp_core_gte_layout, depth_cue_slope) == 0xEC, "GTE depth cue slope");
_Static_assert(sizeof(rp_core_gte_layout) == 0x100, "GTE register prefix");
#endif
