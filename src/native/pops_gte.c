#include "pops_gte.h"
#include <math.h>
#include <string.h>

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

/* Finite-input dot arithmetic used by the VFPU matrix transforms here.
 * Keep two extra product bits with round-to-odd, align by truncation, then
 * drop those bits before final nearest-even normalization. See the pinned
 * PPSSPP arithmetic reference in docs/gte_normal_color.md, not host FMA. */
static float color_dot(const float a[4], const float b[4])
{
    uint32_t products[4], signs[4];
    int exponents[4], largest = -254;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t x, y;
        memcpy(&x, &a[i], sizeof(x)); memcpy(&y, &b[i], sizeof(y));
        const unsigned xe = (x >> 23) & 255, ye = (y >> 23) & 255;
        const uint64_t exact = (uint64_t)((x & 0x7FFFFF) | 0x800000) *
                               ((y & 0x7FFFFF) | 0x800000);
        products[i] = xe && ye ? (uint32_t)(exact >> 21) | ((exact & 0x1FFFFF) != 0) : 0;
        exponents[i] = xe && ye ? (int)xe + (int)ye - 254 : -254;
        signs[i] = (x ^ y) >> 31;
        if (exponents[i] > largest) largest = exponents[i];
    }
    int64_t total = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned shift = (unsigned)(largest - exponents[i]);
        const int64_t term = shift >= 32 ? 0 : products[i] >> shift;
        total += signs[i] ? -term : term;
    }
    uint32_t magnitude = (uint32_t)(total < 0 ? -total : total) >> 2;
    const uint32_t sign = (uint32_t)(total < 0) << 31;
    int top = 0;
    for (uint32_t v = magnitude; v >>= 1;) ++top;
    const int shift = top - 23;
    largest += shift;
    if (magnitude && shift > 0) {
        const uint32_t halfway = 1u << (shift - 1);
        const uint32_t remainder = magnitude & ((1u << shift) - 1);
        magnitude >>= shift;
        magnitude += remainder > halfway || (remainder == halfway && (magnitude & 1));
        if (magnitude == 0x1000000) { magnitude >>= 1; ++largest; }
    } else if (magnitude && shift < 0) {
        magnitude <<= -shift;
    }
    uint32_t bits;
    if (!magnitude || largest <= -127) bits = sign;
    else if (largest >= 128) bits = sign | 0x7F800000;
    else bits = sign | (uint32_t)(largest + 127) << 23 | (magnitude & 0x7FFFFF);
    float result; memcpy(&result, &bits, sizeof(result));
    return result;
}

static float color_limit(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

/* VF2IN rounds to nearest-even and saturates at the signed integer limits.
 * Power-of-two scaling is explicit; this does not use the host rounding mode. */
static int32_t color_to_integer(float value, unsigned scale)
{
    const float scaled = ldexpf(value, (int)scale);
    if (!(scaled < 2147483648.0f)) return INT32_MAX;
    if (scaled <= -2147483648.0f) return INT32_MIN;
    int32_t result = (int32_t)scaled;
    const float fraction = scaled - (float)result;
    const bool odd = ((uint32_t)result & 1) != 0;
    if (fraction > 0.5f || (fraction == 0.5f && odd)) ++result;
    if (fraction < -0.5f || (fraction == -0.5f && odd)) --result;
    return result;
}

static float color_matrix_value(rp_context *c, uint32_t base, unsigned row, unsigned col)
{
    const uint32_t field = base + (row * 3 + col) * sizeof(int16_t);
    /* The ninth coefficient is loaded as a complete signed word by VI2F.S;
     * the first eight are extracted as signed halfwords by VS2I.P. */
    const int32_t raw = row == 2 && col == 2 ? (int32_t)rp_u32(c, field) : signed_half(c, field);
    return ldexpf((float)raw, -12);
}

/* +0xFA68..+0xFBA4: normal color/depth-cue single. Preserve the VFPU stage
 * boundaries, per-channel FLAG accumulation and packed RGB FIFO writes.
 * This remains a reviewed partial model, not hardware-precision proof. */
void rp_pops_gte_ncds(rp_context *c)
{
    rp_function(c, RP_GTE_NCDS_HELPER, "pops.GTE_NCDS_partial");
    if (!c->vfpu_zero_ready) rp_block(c, "GTE_color_vector_constants_not_initialized", RP_GTE_NCDS_HELPER);
    float normal[4] = {0}, light[4] = {0};
    uint32_t flags = 0;
    const uint32_t color = rp_u32(c, RP_GTE_ADDRESS(c, source_color));
    const float ir0 = ldexpf((float)(int32_t)rp_u32(c, RP_GTE_ADDRESS(c, ir[0])), -12);
    for (unsigned i = 0; i < 3; ++i)
        normal[i] = ldexpf((float)signed_half(c, RP_GTE_ADDRESS(c, vectors[0].component[i])), -12);
    for (unsigned row = 0; row < 3; ++row) {
        float matrix[4] = {0};
        for (unsigned col = 0; col < 3; ++col)
            matrix[col] = color_matrix_value(c, RP_GTE_ADDRESS(c, light_matrix), row, col);
        const float transformed = color_dot(matrix, normal);
        light[row] = color_limit(transformed, 0, c->vfpu_reset_rows[row][3]);
        if (light[row] != transformed) flags |= 1u << (24 - row);
    }
    light[3] = 1;
    uint32_t packed = color & 0xFF000000;
    for (unsigned row = 0; row < 3; ++row) {
        float matrix[4];
        for (unsigned col = 0; col < 3; ++col)
            matrix[col] = color_matrix_value(c, RP_GTE_ADDRESS(c, color_matrix), row, col);
        matrix[3] = ldexpf((float)(int32_t)rp_u32(c, RP_GTE_ADDRESS(c, background_color[row])), -12);
        const float transformed = color_dot(matrix, light);
        const float maximum = c->vfpu_reset_rows[row][3];
        const float lit = color_limit(transformed, 0, maximum);
        if (lit != transformed) flags |= 1u << (24 - row);
        const float intensity = (float)((color >> (row * 8)) & 255) * (1.0f / 256.0f);
        volatile float modulated = intensity * lit;
        const float far = ldexpf((float)(int32_t)rp_u32(c, RP_GTE_ADDRESS(c, far_color[row])), -12);
        const float difference = far - modulated;
        const float limited = color_limit(difference, c->vfpu_reset_rows[row][2], maximum);
        if (fabsf(difference) >= maximum) flags |= 1u << (24 - row);
        volatile float cue = limited * ir0;
        const float value = modulated + cue;
        const float ir = color_limit(value, 0, maximum);
        if (value != ir) flags |= 1u << (24 - row);
        if (value != color_limit(value, 0, 1)) flags |= 1u << (21 - row);
        rp_w32(c, RP_GTE_ADDRESS(c, mac[row + 1]), (uint32_t)color_to_integer(value, 12));
        rp_w32(c, RP_GTE_ADDRESS(c, ir[row + 1]), (uint32_t)color_to_integer(ir, 12));
        const int32_t byte_source = color_to_integer(value, 31);
        if (byte_source > 0) packed |= ((uint32_t)byte_source >> 23) << (row * 8);
    }
    const uint32_t old1 = rp_u32(c, RP_GTE_ADDRESS(c, color_fifo[1]));
    const uint32_t old2 = rp_u32(c, RP_GTE_ADDRESS(c, color_fifo[2]));
    rp_w32(c, RP_GTE_ADDRESS(c, color_fifo[0]), old1);
    rp_w32(c, RP_GTE_ADDRESS(c, color_fifo[1]), old2);
    rp_w32(c, RP_GTE_ADDRESS(c, color_fifo[2]), packed);
    c->vfpu_s330_bits = flags;
    c->run_gpr[2] = color & 255;
    c->run_gpr[4] = color >> 1;
    c->run_gpr[5] = (color >> 8) & 255;
    c->run_gpr[6] = (color >> 16) & 255;
    rp_event(c, "milestone", "GTE_NCDS_color_published", packed, flags);
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

/* +0x10BF0/+0x10C38. Preserve the original MFLO-before-SRA truncation and
 * halfword OTZ store, including the high padding halfword left untouched. */
void rp_pops_gte_avsz(rp_context *c, bool four_vertices)
{
    const uint32_t helper = four_vertices ? RP_GTE_AVSZ4_HELPER : RP_GTE_AVSZ3_HELPER;
    rp_function(c, helper, four_vertices ? "pops.GTE_AVSZ4" : "pops.GTE_AVSZ3");
    uint32_t sum = 0;
    for (unsigned i = four_vertices ? 0 : 1; i < 4; ++i)
        sum += read_half(c, RP_GTE_ADDRESS(c, depth[i].value));
    const int32_t scale = signed_half(c, four_vertices ?
        RP_GTE_ADDRESS(c, depth_scale4) : RP_GTE_ADDRESS(c, depth_scale3));
    const int64_t product = (int64_t)sum * scale;
    c->run_lo = (uint32_t)product;
    c->run_hi = (uint32_t)((uint64_t)product >> 32);
    const int32_t scaled = (int32_t)c->run_lo >> 12;
    const uint32_t flags = (uint32_t)((uint32_t)scaled > 0xFFFF) << 18;
    const uint16_t depth = (uint16_t)clamp(scaled, 0, 0xFFFF);
    rp_w32(c, RP_GTE_ADDRESS(c, mac[0]), c->run_lo);
    write_half(c, RP_GTE_ADDRESS(c, ordering_depth), depth);
    c->vfpu_s330_bits = flags;
    c->run_gpr[2] = 0xFFFF;
    c->run_gpr[4] = depth;
    c->run_gpr[5] = flags;
    c->run_gpr[6] = read_half(c, RP_GTE_ADDRESS(c, depth[3].value));
    rp_event(c, "milestone", "GTE_ordering_depth_computed", helper, depth);
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
