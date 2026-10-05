#include "runtime.h"

#define SPU_SHARED UINT32_C(0x49F40000)

static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void put_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* Original 22-us thread delay yields to the ME. The host takes one cooperative
 * worker step instead; this does not establish hardware sample pacing.
 */
static void pace_writer(rp_context *c)
{
    const uint32_t now = rp_u32(c, c->gp + 0x1AC) - rp_u32(c, c->gp + 0x1B0);
    const uint32_t elapsed = now - rp_u32(c, c->gp + 0x378);
    rp_w32(c, c->gp + 0x378, now);
    if ((int32_t)elapsed >= 0x300 &&
            rp_u32(c, SPU_SHARED + 0x294) == rp_u32(c, c->gp + 0x37C)) {
        ++c->services;
        rp_event(c, "headless_adapter", "SPU_writer_delay_cooperative_ME_step", 0x7F00, 22);
        if (c->me_callback) rp_pops_me_poll(c);
    }
    rp_w32(c, c->gp + 0x37C, rp_u32(c, SPU_SHARED + 0x294));
}

static void lock_writer(rp_context *c)
{
    rp_w8(c, SPU_SHARED + 0x29D, 1);
    rp_w8(c, SPU_SHARED + 0x29F, 1);
    if (half(c, SPU_SHARED + 0x29E) == 0x101)
        rp_block(c, "SPU_writer_waiting_for_ME_release", 0x7FC8);
}
static void unlock_writer(rp_context *c)
{
    rp_w8(c, SPU_SHARED + 0x29D, 0);
}

static void write_register_half(rp_context *c, uint32_t reg, uint16_t value)
{
    const uint32_t gp = c->gp;
    rp_w32(c, gp + 0x1B0, rp_u32(c, gp + 0x1B0) - 1);
    if (reg == 0x1AE) return;
    if (reg < 0x180 && (reg & 0xE) == 6 && value < 0x200) value = 0x202;
    put_half(c, SPU_SHARED + reg, value);
    rp_event(c, "spu_register_write", "shared_register", reg, value);

    if (reg < 0x180) {
        pace_writer(c);
        const uint32_t voice = UINT32_C(1) << (reg >> 4);
        lock_writer(c);
        rp_w32(c, SPU_SHARED + 0x288, rp_u32(c, SPU_SHARED + 0x288) | voice);
        if ((reg & 15) == 14)
            rp_w32(c, SPU_SHARED + 0x2A0, rp_u32(c, SPU_SHARED + 0x2A0) | voice);
        unlock_writer(c);
    } else if (reg == 0x1A4) {
        if ((half(c, SPU_SHARED + 0x1AA) & 0x40) && rp_u32(c, gp + 0x34C) >> 2 == value)
            rp_w8(c, gp + 0x34A, 0x40);
    } else if (reg - 0x188 < 4) {
        if (!value) return;
        pace_writer(c);
        const uint32_t mask = (uint32_t)value << ((reg * 8 - 0xC40) & 31);
        lock_writer(c);
        rp_w32(c, SPU_SHARED + 0x280, rp_u32(c, SPU_SHARED + 0x280) | mask);
        rp_w32(c, SPU_SHARED + 0x284, rp_u32(c, SPU_SHARED + 0x284) & ~mask);
        unlock_writer(c);
    } else if (reg - 0x18C < 4) {
        if (!value) return;
        pace_writer(c);
        const uint32_t mask = (uint32_t)value << ((reg * 8 - 0xC60) & 31);
        lock_writer(c);
        rp_w32(c, SPU_SHARED + 0x284, rp_u32(c, SPU_SHARED + 0x284) | mask);
        unlock_writer(c);
    } else if (reg == 0x1A8) {
        const uint8_t queued = *(uint8_t *)rp_memory(c, gp + 0x34B, 1);
        if (queued < 32) {
            put_half(c, gp + 0x308 + queued * 2, value);
            rp_w8(c, gp + 0x34B, (uint8_t)(queued + 1));
        }
    } else if (reg - 0x190 < 12) {
        pace_writer(c);
    } else if (reg == 0x1AA) {
        put_half(c, gp + 0x348, value);
        pace_writer(c);
        if (!(value & 0x8000)) put_half(c, SPU_SHARED + reg, value & 0x3F);
        if (((value >> 4) & 3) == 1) {
            const uint32_t irq_word = (uint32_t)half(c, SPU_SHARED + 0x1A4) << 2;
            const uint8_t count = *(uint8_t *)rp_memory(c, gp + 0x34B, 1);
            uint32_t cursor = rp_u32(c, gp + 0x34C);
            for (unsigned i = 0; i < count; ++i) {
                if (cursor == irq_word) rp_w8(c, gp + 0x34A, 0x40);
                put_half(c, SPU_SHARED + 0x2C0 + cursor * 2, half(c, gp + 0x308 + i * 2));
                cursor = (cursor + 1) & 0x3FFFF;
            }
            rp_w32(c, gp + 0x34C, cursor);
            rp_w8(c, gp + 0x34B, 0);
        }
        if (!(value & 0x40)) {
            if (rp_u32(c, gp + 0x354)) rp_pops_remove_event(c, gp + 0x350);
            rp_w8(c, SPU_SHARED + 0x292, 1);
            rp_w8(c, gp + 0x34A, 0);
        } else if (!rp_u32(c, gp + 0x354)) {
            rp_pops_schedule_event(c, gp + 0x350, 0x869);
        }
    } else if (reg == 0x1A6) {
        rp_w32(c, gp + 0x34C, (uint32_t)value << 2);
        if ((half(c, SPU_SHARED + 0x1AA) & 0x40) && value == half(c, SPU_SHARED + 0x1A4))
            rp_w8(c, gp + 0x34A, 0x40);
    } else if (reg == 0x1A2 || reg - 0x1C0 < 0x40) {
        lock_writer(c);
        rp_w32(c, SPU_SHARED + 0x288, rp_u32(c, SPU_SHARED + 0x288) | 0x80000000);
        unlock_writer(c);
    }
}

/* +0x7F00. Byte writes zero the other byte rather than merging it. A word
 * write runs the low half first, then the high half with another cycle debit.
 */
void rp_pops_spu_write_register(rp_context *c, uint32_t address, uint32_t value, uint32_t width)
{
    rp_function(c, 0x7F00, "pops.spu_write_register");
    uint32_t reg = address & 0x1FF;
    if (width == 0) {
        value &= 0xFF;
        if (reg & 1) { value <<= 8; --reg; }
    } else if (width != 1) {
        rp_pops_spu_write_register(c, reg, value, 1);
        reg += 2; value >>= 16;
    }
    write_register_half(c, reg, (uint16_t)value);
}

/* +0x85F4. SPUSTAT merges the shared status and IRQ latch; reading it does
 * not acknowledge the IRQ or wait for the Media Engine. */
uint32_t rp_pops_spu_read_register(rp_context *c, uint32_t address, uint32_t width)
{
    rp_function(c, 0x85F4, "pops.spu_read_register");
    const uint32_t reg = address & 0x3FF;
    const uint32_t shared = SPU_SHARED + reg;
    rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - 10);
    if (reg == 0x1AE && (width & 3) == 1) {
        const uint32_t status = half(c, shared) & 0x0F7F;
        const uint32_t irq = *(uint8_t *)rp_memory(c, c->gp + 0x34A, 1);
        return status | irq | (((status >> 5) & 1) << 7);
    }
    if (width == 2) {
        rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - 7);
        return rp_u32(c, shared);
    }
    if (width == 1 || width == 5) {
        const uint32_t value = half(c, shared);
        return width == 1 && (value & 0x8000) ? value | UINT32_C(0xFFFF0000) : value;
    }
    const uint32_t value = *(uint8_t *)rp_memory(c, shared, 1);
    return width == 0 && (value & 0x80) ? value | UINT32_C(0xFFFFFF00) : value;
}
