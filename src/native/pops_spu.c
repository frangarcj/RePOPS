#include "runtime.h"
#include <string.h>

#define SHARED UINT32_C(0x49F40000)
#define MIXER UINT32_C(0x09FF0000)

static uint16_t read_half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void write_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* POPS offset zero: entry -> +0x1968 -> +0x9D4, SPU disabled only.
 * This branch includes its state writes and return value; it is not a generic
 * silence stub. Enabled mixing and the producer busy-wait are not implemented.
 * Those unsupported domains are refused before touching shared producer state.
 */
bool rp_pops_spu_inactive_sample(rp_context *c, uint32_t *packed)
{
    rp_function(c, 0, "pops.spu_disabled_sample_path");
    const uint16_t control = read_half(c, SHARED + 0x1AA);
    if ((control & 0x8000) || (rp_u32(c, SHARED + 0x29C) & 0xFFFF) == 0x100)
        return false;

    write_half(c, SHARED + 0x29E, 1);
    rp_w32(c, SHARED + 0x294, rp_u32(c, SHARED + 0x294) + 1);
    uint32_t result = rp_u32(c, SHARED + 0x18C);
    rp_w8(c, SHARED + 0x29E, 0);
    const uint8_t notification = *(uint8_t *)rp_memory(c, SHARED + 0x293, 1);
    write_half(c, SHARED + 0x290, 0);
    if (notification == 0xFF) rp_w8(c, SHARED + 0x293, 0xFE);

    if (read_half(c, MIXER + 0x1790) & 0x8000) {
        write_half(c, MIXER + 0x1790, control & 0x3F);
        memset(rp_memory(c, MIXER + 0x858, 0xAE0), 0, 0xAE0);
        for (unsigned voice = 0; voice < 24; ++voice)
            write_half(c, SHARED + 0xC + voice * 0x10, 0);
        result = 0;
        rp_w8(c, MIXER + 0x1788, 0);
        rp_w32(c, MIXER + 0x1780, 0);
        rp_w32(c, MIXER + 0x13EC, 0);
        rp_w32(c, SHARED + 0x280, 0);
        rp_w32(c, SHARED + 0x284, 0);
    }

    const uint32_t old_index = read_half(c, MIXER + 0x13E8);
    const uint32_t cursor = rp_u32(c, MIXER + 0x1798);
    const uint32_t difference = cursor - (SHARED + 0x2C0 + old_index * 2);
    const uint8_t latch = *(uint8_t *)rp_memory(c, MIXER + 0x1797, 1) |
                           ((difference & UINT32_C(0xFFFFF3FF)) == 0);
    const uint32_t next_index = (old_index + 1) & 0x1FF;
    rp_w8(c, MIXER + 0x1797, latch);
    write_half(c, MIXER + 0x13E8, (uint16_t)next_index);
    rp_w32(c, SHARED + 0x19C, rp_u32(c, MIXER + 0x13E4));
    write_half(c, SHARED + 0x1AE,
               (uint16_t)((control & 0x3F) | ((latch != 0) << 6) | ((next_index >> 8) << 11)));
    *packed = result;
    rp_event(c, "milestone", "spu_disabled_callback_returned_with_state_updates", 0, result);
    return true;
}

static int32_t signed_half(uint32_t value)
{
    return (int32_t)(value & 0x7FFF) - (int32_t)(value & 0x8000);
}

/* Active callback entry through the first voice's control updates. This is
 * a partial invocation: it stops at the unreconstructed sample path and never
 * reports a produced sample. The disabled callback remains independently usable.
 */
static void active_sample_prefix(rp_context *c, uint16_t control)
{
    rp_function(c, 0, "pops.spu_active_sample_prefix");
    write_half(c, SHARED + 0x29E, 1);
    rp_w32(c, SHARED + 0x294, rp_u32(c, SHARED + 0x294) + 1);
    const uint32_t dirty = rp_u32(c, SHARED + 0x288);
    const uint32_t repeat = rp_u32(c, SHARED + 0x2A0);
    const uint32_t keyon = rp_u32(c, SHARED + 0x280);
    const uint32_t keyoff = rp_u32(c, SHARED + 0x284);
    rp_w32(c, SHARED + 0x18C, rp_u32(c, SHARED + 0x18C) & ~keyon);
    rp_w32(c, SHARED + 0x288, 0); rp_w32(c, SHARED + 0x2A0, 0);
    rp_w32(c, SHARED + 0x280, 0); rp_w32(c, SHARED + 0x284, 0);
    rp_w32(c, SHARED + 0x188, 0);
    rp_w8(c, SHARED + 0x29E, 0);
    rp_w32(c, MIXER + 0x13E4, rp_u32(c, SHARED + 0x19C) & ~keyon);
    if (*(uint8_t *)rp_memory(c, SHARED + 0x292, 1)) {
        rp_w8(c, MIXER + 0x1797, 0); rp_w8(c, SHARED + 0x292, 0);
    }
    const uint32_t irq_cursor = control & 0x40 ?
        SHARED + 0x2C0 + ((uint32_t)read_half(c, SHARED + 0x1A4) << 3) : SHARED + 0x2BE;
    rp_w32(c, MIXER + 0x1798, irq_cursor);
    const uint16_t previous = read_half(c, MIXER + 0x1790);
    int32_t count = signed_half(read_half(c, MIXER + 0x178C));
    if ((control ^ previous) & 0x3F00) count = 0;
    --count;
    write_half(c, MIXER + 0x1790, control);
    if (count <= 0) {
        count = (int32_t)(0x4000u >> ((control >> 10) & 15));
        bool advance = count == 0;
        if (!advance) {
            const uint8_t phase = *(uint8_t *)rp_memory(c, MIXER + 0x178E, 1);
            const uint32_t taps = UINT32_C(0xFFFFEEAA) >> (((control >> 8) & 3) * 4);
            rp_w8(c, MIXER + 0x178E, (uint8_t)((phase >> 1) | (phase << 7)));
            advance = (phase & taps) != 0;
        }
        if (advance) {
            const int32_t noise = signed_half(read_half(c, MIXER + 0x178A));
            const uint32_t feedback = (0x69u >> (((uint32_t)noise >> 10) & 7)) ^
                                       (uint32_t)(noise >> 15);
            write_half(c, MIXER + 0x178A, (uint16_t)(((uint32_t)noise << 1) | (feedback & 1)));
        }
    }
    write_half(c, MIXER + 0x178C, (uint16_t)count);
    rp_event(c, "milestone", "ME_active_mailbox_consumed", dirty, keyon);

    /* +0x17F0/+0x1774/+0x1708 for voice zero, reached before that voice's
     * sample/envelope processing. Later voices must not be updated early. */
    const uint32_t voice = MIXER + 0x858;
    if (dirty & 1) {
        write_half(c, voice + 0x30, read_half(c, SHARED + 4));
        if (repeat & 1) {
            rp_w8(c, voice + 0x2E, 1);
            write_half(c, voice + 0x32, read_half(c, SHARED + 0xE) & 0xFFFE);
        }
        rp_w32(c, voice + 0x18, rp_u32(c, SHARED + 8));
        const uint32_t volumes = rp_u32(c, SHARED);
        if (volumes != rp_u32(c, voice + 0x14)) {
            rp_w32(c, voice + 0x14, volumes);
            if (volumes & 0x8000) rp_block(c, "ME_left_volume_sweep_not_reconstructed", 0x18D4);
            write_half(c, voice, (uint16_t)(volumes << 1));
            write_half(c, voice + 6, 0);
            if (volumes & UINT32_C(0x80000000))
                rp_block(c, "ME_right_volume_sweep_not_reconstructed", 0x1854);
            write_half(c, voice + 0xA, (uint16_t)((volumes >> 16) << 1));
            write_half(c, voice + 0x10, 0);
        }
    }
    int32_t state = (int8_t)*(uint8_t *)rp_memory(c, voice + 0x1C, 1);
    if (keyon & 1) {
        write_half(c, SHARED + 0xC, 0);
        rp_w32(c, voice + 0x2C, read_half(c, SHARED + 6) & 0xFFFE);
        write_half(c, voice + 0x26, 0); rp_w32(c, voice + 0x28, 0);
        if (*(uint8_t *)rp_memory(c, MIXER + 0x1794, 1) == 0) {
            const int32_t relative = (int8_t)*(uint8_t *)rp_memory(c, MIXER + 0x1795, 1);
            const uint32_t paired = voice + (uint32_t)(relative * 0x74);
            rp_w32(c, paired + 0x28, UINT32_C(0xFFFE4000));
            write_half(c, paired + 0x2C, read_half(c, MIXER + 0x1792));
            rp_w8(c, MIXER + 0x1797, 0);
        }
        rp_w32(c, voice + 0x20, 4);
        rp_w32(c, voice + 0x70, 0); rp_w32(c, voice + 0x6C, 0);
        state = 5; rp_w32(c, voice + 0x1C, 5);
    }
    if ((keyoff & 1) && state < 24) {
        const uint32_t adsr2 = read_half(c, voice + 0x1A);
        const int32_t shift = (int32_t)(adsr2 & 31) - 11;
        uint32_t step = 0xFFF8, period = 1;
        rp_w8(c, voice + 0x1C, 24); write_half(c, voice + 0x1E, 0);
        if (shift > 0) {
            period = (UINT32_C(1) << shift) & 0xFFFF;
            if (!period) step = 0;
        } else {
            const unsigned rotate = (uint32_t)shift & 31;
            step = (step >> rotate) | (step << ((32 - rotate) & 31));
        }
        write_half(c, voice + 0x24, (uint16_t)period);
        const uint32_t exponential = step ? (rp_u32(c, voice + 0x18) >> 21) & 1 : 0;
        if (exponential) step &= 0x7FF8;
        write_half(c, voice + 0x22, (uint16_t)step);
        write_half(c, voice + 0x20, (uint16_t)period);
        rp_w8(c, voice + 0x1D, (uint8_t)exponential);
        rp_event(c, "milestone", "ME_first_voice_release_initialized", voice, period);
        rp_block(c, "ME_voice_sample_path_not_reconstructed", 0x11CC);
    }
    rp_event(c, "milestone", "ME_first_voice_control_initialized", voice, (uint32_t)state);
    rp_block(c, "ME_voice_sample_path_not_reconstructed", 0x1E4);
}

bool rp_pops_spu_sample(rp_context *c, uint32_t *packed)
{
    const uint16_t control = read_half(c, SHARED + 0x1AA);
    if (!(control & 0x8000)) return rp_pops_spu_inactive_sample(c, packed);
    if ((rp_u32(c, SHARED + 0x29C) & 0xFFFF) == 0x100) return false;
    active_sample_prefix(c, control);
    return false;
}
