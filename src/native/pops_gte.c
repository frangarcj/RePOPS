#include "pops_gte.h"

static uint16_t read_half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void write_half(rp_context *c, uint32_t address, uint16_t value)
{
    rp_w8(c, address, (uint8_t)value);
    rp_w8(c, address + 1, (uint8_t)(value >> 8));
}
static int32_t signed_half(rp_context *c, uint32_t address)
{
    return (int16_t)read_half(c, address);
}
static int32_t clamp(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

/* +0x10B34..+0x10B74. The raw MSUB word at +0x10B60 uses A2 and T0;
 * the historical text listing incorrectly prints a zero source. POPS clears
 * S330 even when the determinant exceeds signed 32-bit MAC0. */
void rp_pops_gte_nclip(rp_context *c)
{
    rp_function(c, RP_GTE_NCLIP_HELPER, "pops.GTE_NCLIP");
    const int32_t x0 = signed_half(c, RP_GTE_ADDRESS(c, screen[0].x));
    const int32_t y0 = signed_half(c, RP_GTE_ADDRESS(c, screen[0].y));
    const int32_t x1 = signed_half(c, RP_GTE_ADDRESS(c, screen[1].x));
    const int32_t y1 = signed_half(c, RP_GTE_ADDRESS(c, screen[1].y));
    const int32_t x2 = signed_half(c, RP_GTE_ADDRESS(c, screen[2].x));
    const int32_t y2 = signed_half(c, RP_GTE_ADDRESS(c, screen[2].y));
    const int64_t determinant = (int64_t)(x1 - x0) * (y2 - y0) -
                                (int64_t)(x2 - x0) * (y1 - y0);
    c->run_lo = (uint32_t)determinant;
    c->run_hi = (uint32_t)((uint64_t)determinant >> 32);
    c->run_gpr[2] = (uint32_t)y1;
    c->run_gpr[4] = c->run_lo;
    c->run_gpr[5] = (uint32_t)y0;
    c->run_gpr[6] = (uint32_t)(x2 - x0);
    c->run_gpr[8] = (uint32_t)(y1 - y0);
    c->run_gpr[28] = 0x10000;
    c->vfpu_s330_bits = 0;
    rp_w32(c, RP_GTE_ADDRESS(c, mac[0]), c->run_lo);
    rp_event(c, "milestone", "GTE_NCLIP_area_computed", RP_GTE_NCLIP_HELPER, c->run_lo);
}

/* +0x10B14/+0x10B24 enter the shared three-vector projection loops.
 * Retain POPS's low-MAC-word truncation before translation, its reciprocal
 * table and halfword FIFO writes. This is not a replacement PS1 GTE formula.
 * Scratch GPRs are not outputs of this C interface; the emitter flushes them. */
void rp_pops_gte_rtpt(rp_context *c, bool update_flags)
{
    rp_function(c, update_flags ? RP_GTE_RTPT_FLAGS_HELPER : RP_GTE_RTPT_NO_FLAGS_HELPER,
                update_flags ? "pops.GTE_RTPT_with_flags" : "pops.GTE_RTPT_without_flags");
    write_half(c, RP_GTE_ADDRESS(c, depth[0].value), read_half(c, RP_GTE_ADDRESS(c, depth[3].value)));
    uint32_t flags = 0;
    int32_t mac[3] = {0}, ir[3] = {0}, ratio = 0;
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        int32_t input[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            input[axis] = signed_half(c, RP_GTE_ADDRESS(c, vectors[vertex].component[axis]));
        for (unsigned row = 0; row < 3; ++row) {
            int64_t product = 0;
            for (unsigned col = 0; col < 3; ++col)
                product += (int64_t)signed_half(c, RP_GTE_ADDRESS(c, rotation[row][col])) * input[col];
            const int32_t shifted = (int32_t)(uint32_t)product >> 12;
            mac[row] = (int32_t)((uint32_t)shifted + rp_u32(c, RP_GTE_ADDRESS(c, translation[row])));
            ir[row] = clamp(mac[row], -32768, 32767);
            if (row < 2) {
                rp_w32(c, RP_GTE_ADDRESS(c, mac[row + 1]), (uint32_t)mac[row]);
                if (mac[row] != ir[row]) flags |= 1u << (24 - row);
            }
        }
        const uint32_t h = read_half(c, RP_GTE_ADDRESS(c, projection_distance));
        if ((uint32_t)mac[2] > 0xFFFF) flags |= 1u << 18;
        uint16_t depth;
        if (mac[2] > (int32_t)(h >> 1)) {
            depth = (uint16_t)clamp(mac[2], 0, 0xFFFF);
            unsigned leading = 0;
            for (uint32_t n = depth; !(n & 0x80000000u); n <<= 1) ++leading;
            const unsigned shift = leading - 14;
            const uint32_t anchor = update_flags ? RP_GTE_RECIPROCAL_ANCHOR : c->run_gpr[29];
            const uint32_t reciprocal = rp_u32(c, anchor - ((uint32_t)depth << shift));
            const uint64_t division = UINT64_C(0x8000) * c->gp +
                                      (uint64_t)(h << shift) * reciprocal;
            ratio = (int32_t)(division >> 32);
            if (ratio > (int32_t)(c->gp | 0xFFFF)) ratio = (int32_t)(c->gp | 0xFFFF);
        } else {
            depth = (uint16_t)clamp(mac[2], 0, 0xFFFF);
            ratio = (int32_t)(c->gp | 0xFFFF);
            flags |= 1u << 17;
        }
        write_half(c, RP_GTE_ADDRESS(c, depth[vertex + 1].value), depth);
        uint16_t screen[2];
        for (unsigned axis = 0; axis < 2; ++axis) {
            const int64_t projection = (int32_t)rp_u32(c, RP_GTE_ADDRESS(c, screen_offset[axis])) +
                                       (int64_t)ir[axis] * ratio;
            const int32_t raw = (int32_t)(uint32_t)((uint64_t)projection >> 16);
            const int32_t saturated = clamp(raw, -1024, 1023);
            if (raw != saturated) flags |= 1u << (14 - axis);
            screen[axis] = (uint16_t)saturated;
        }
        rp_w32(c, RP_GTE_ADDRESS(c, screen[vertex]), screen[0] | ((uint32_t)screen[1] << 16));
    }
    rp_w32(c, RP_GTE_ADDRESS(c, mac[3]), (uint32_t)mac[2]);
    for (unsigned axis = 0; axis < 3; ++axis)
        rp_w32(c, RP_GTE_ADDRESS(c, ir[axis + 1]), (uint32_t)ir[axis]);
    if (mac[2] != ir[2]) flags |= 1u << 22;
    const int64_t cue = (int32_t)rp_u32(c, RP_GTE_ADDRESS(c, depth_cue_intercept)) +
                        (int64_t)signed_half(c, RP_GTE_ADDRESS(c, depth_cue_slope)) * ratio;
    const int32_t cue_shifted = (int32_t)(uint32_t)((uint64_t)cue >> 12);
    c->run_lo = (uint32_t)cue;
    c->run_hi = (uint32_t)((uint64_t)cue >> 32);
    rp_w32(c, RP_GTE_ADDRESS(c, mac[0]), (uint32_t)cue);
    if ((uint32_t)cue_shifted > 4096) {
        flags |= 1u << 12;
        if (cue_shifted > 0x80000) flags |= 1u << 16;
        if (cue_shifted < -0x80001) flags |= 1u << 15;
    }
    rp_w32(c, RP_GTE_ADDRESS(c, ir[0]), (uint32_t)clamp(cue_shifted, 0, 4096));
    /* MTV is a bit transfer into S330, not a floating-point conversion.
     * The no-flags helper deliberately leaves its previous contents alone. */
    if (update_flags) c->vfpu_s330_bits = flags;
    rp_event(c, "milestone", "GTE_RTPT_three_vertices_projected", update_flags,
             rp_u32(c, RP_GTE_ADDRESS(c, screen[2])));
}
