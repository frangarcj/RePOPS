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
