#include "runtime.h"

static uint32_t rotate_right(uint32_t value, unsigned shift)
{
    shift &= 31;
    return (value >> shift) | (value << ((32 - shift) & 31));
}

/* +0x91BC. Newly enabled channels enter +0x8E4C, whose idle prefix is
 * reconstructed here; an actual DMA request remains a separate boundary. */
void rp_pops_dma_control_write(rp_context *c, uint32_t address, uint32_t value, uint32_t width)
{
    rp_function(c, 0x91BC, "pops.write_DMA_control");
    const uint32_t slot = c->gp + 0x20F0 + (address & 4);
    const uint32_t old = rp_u32(c, slot);
    if (!(address & 4)) {
        uint32_t newly_set = value & ~old;
        rp_w32(c, slot, value);
        for (unsigned channel = 0; newly_set; ++channel, newly_set >>= 4) {
            if (!(newly_set & 8)) continue;
            rp_function(c, 0x8E4C, "pops.try_DMA_channel_idle_prefix");
            if (rp_u32(c, c->gp + 0x1EC + channel * 28)) continue;
            const uint32_t chcr = rp_u32(c, c->gp + 0x2088 + channel * 16);
            if (!(chcr & 0x01000000)) continue;
            rp_event(c, "DMA_boundary", "newly_enabled_active_channel", channel, chcr);
            rp_block(c, "active_DMA_channel_not_reconstructed", 0x8E4C);
        }
        return;
    }
    if (width != 2) {
        const unsigned shift = (address * 8) & 31;
        uint32_t merged = rotate_right(old & UINT32_C(0x80FF7FFF), shift);
        const uint32_t mask = width == 0 ? 0xFF : 0xFFFF;
        merged = (merged & ~mask) | (value & mask);
        value = rotate_right(merged, 0u - shift);
    }
    value ^= ((old & ~value) ^ value) & UINT32_C(0x7F008000);
    const uint32_t pending = ((value >> 24) & 0x7F) ? ((value >> 23) & 1) : 0;
    rp_w32(c, slot, (value & UINT32_C(0x7FFFFFFF)) | (pending << 31));
}

uint32_t rp_pops_irq_read(rp_context *c, uint32_t address)
{
    rp_function(c, 0x9850, "pops.read_interrupt_register");
    uint32_t downcount = rp_u32(c, c->gp + 0x1B0);
    uint32_t now = rp_u32(c, c->gp + 0x1AC) - downcount;
    uint32_t last_advance = 4;
    if ((int32_t)downcount > 0 &&
        (int32_t)(now - rp_u32(c, c->gp + 0x1C4)) < 40) {
        uint32_t advance = (uint32_t)*(uint8_t *)rp_memory(c, c->gp + 0x1C3, 1) * 2;
        if (advance > downcount) advance = downcount;
        if (advance > 80) advance = 80;
        downcount -= advance;
        rp_w32(c, c->gp + 0x1C8, rp_u32(c, c->gp + 0x1C8) + advance);
        now += advance;
        rp_w32(c, c->gp + 0x1B0, downcount);
        last_advance = advance;
    }
    rp_w8(c, c->gp + 0x1C3, (uint8_t)last_advance);
    rp_w32(c, c->gp + 0x1C4, rp_u32(c, c->gp + 0x1C8));
    rp_w32(c, c->gp + 0x1C8, now);
    return rp_u32(c, c->gp + (address & 0xFFC) + 0x2000);
}

void rp_pops_timer_write(rp_context *c, uint32_t address, uint32_t value)
{
    rp_function(c, 0x9C60, "pops.write_timer_register_partial");
    rp_event(c, "timer_write", "register_value", address, value);
    const uint32_t channel = (address >> 4) & 3;
    const uint32_t reg = address & 0xF;
    if (channel >= 3)
        rp_block(c, "timer_write_path_not_reconstructed", address);
    const uint32_t timer = c->gp + 0x64C + channel * 0x20;
    if (reg == 4 && (value & 0xFFFF) == 0) {
        const uint32_t mode = rp_u32(c, timer + 0x18) & ~UINT32_C(0x3FF);
        rp_w32(c, timer + 0x18, mode);
        rp_w32(c, timer + 0x10, rp_u32(c, timer + 0x10) & 0x1FFFF);
        rp_w32(c, timer + 0x14, rp_u32(c, c->gp + 0x1AC) - rp_u32(c, c->gp + 0x1B0));
        rp_w8(c, timer + 0x1D, 0);
        return;
    }
    if (reg == 8) {
        const uint32_t target = (value & 0xFFFF) ? (value & 0xFFFF) : 0x10000;
        const uint32_t old = rp_u32(c, timer + 0x10);
        if ((old & 0x3FFFFFFF) != target)
            rp_block(c, "timer_target_reschedule_not_reconstructed", address);
        return;
    }
    if (reg == 0 && (value & 0xFFFF) == 0) {
        if (rp_u32(c, timer + 4))
            rp_block(c, "timer_counter_event_reschedule_not_reconstructed", address);
        if (rp_u32(c, timer + 0x18) & 0x30)
            rp_block(c, "timer_counter_mode_not_reconstructed", address);
        rp_w32(c, timer + 0x14,
               rp_u32(c, c->gp + 0x1AC) - rp_u32(c, c->gp + 0x1B0));
        return;
    }
    rp_block(c, "timer_write_path_not_reconstructed", address);
}

/* +0x98C4: I_STAT acknowledges with AND; I_MASK replaces the mask. Only a
 * change in pending state updates COP0 cause and potentially brings an event
 * deadline forward. Access-width arguments are not used by the original.
 */
void rp_pops_irq_write(rp_context *c, uint32_t address, uint32_t value)
{
    rp_function(c, 0x98C4, "pops.write_interrupt_register");
    const uint32_t old_status = rp_u32(c, c->gp + 0x2070);
    const uint32_t old_mask = rp_u32(c, c->gp + 0x2074);
    const uint32_t offset = address & 12;
    if (!offset) value &= old_status;
    rp_w32(c, c->gp + 0x2070 + offset, value);
    const bool was_pending = (old_status & old_mask) != 0;
    const bool pending = (rp_u32(c, c->gp + 0x2070) & rp_u32(c, c->gp + 0x2074)) != 0;
    if (pending != was_pending) {
        const uint32_t cause = (rp_u32(c, c->gp + 0x134) & ~UINT32_C(0x400)) | (uint32_t)pending << 10;
        const uint32_t status = rp_u32(c, c->gp + 0x130);
        rp_w32(c, c->gp + 0x134, cause);
        if ((status & 1) && (status & cause & 0xFF00)) {
            const uint32_t remaining = rp_u32(c, c->gp + 0x1B0);
            rp_w32(c, c->gp + 0x1B0, 0);
            rp_w32(c, c->gp + 0x1AC, rp_u32(c, c->gp + 0x1AC) - remaining);
        }
    }
}

/* +0x9668: unlink and adjust the remaining time when removing the first node. */
void rp_pops_remove_event(rp_context *c, uint32_t event)
{
    rp_function(c, 0x9668, "pops.remove_guest_event");
    const uint32_t next = rp_u32(c, event), previous = rp_u32(c, event + 4);
    rp_w32(c, next + 4, previous);
    rp_w32(c, previous, next);
    const uint32_t downcount = rp_u32(c, c->gp + 0x1B0);
    if (previous == c->gp + 0x1B8 && (int32_t)downcount > 0) {
        const uint32_t deadline = rp_u32(c, next + 8);
        const uint32_t old_deadline = rp_u32(c, c->gp + 0x1AC);
        rp_w32(c, c->gp + 0x1AC, deadline);
        rp_w32(c, c->gp + 0x1B0, downcount + deadline - old_deadline);
    }
    rp_w32(c, event + 4, 0);
}

/* +0x953C: consume the original intrusive event list. Time is guest cycles,
 * including a callback's debit; no wall-clock sleep or synthetic frame tick.
 */
uint32_t rp_pops_dispatch_events(rp_context *c)
{
    rp_function(c, 0x953C, "pops.dispatch_due_events");
    const uint32_t head = c->gp + 0x1B8;
    uint32_t now = rp_u32(c, c->gp + 0x1AC) - rp_u32(c, c->gp + 0x1B0);
    uint32_t remaining = 0;
    unsigned dispatched = 0;
    for (;;) {
        const uint32_t event = rp_u32(c, head);
        const uint32_t deadline = rp_u32(c, event + 8);
        remaining = deadline - now;
        rp_w32(c, c->gp + 0x1AC, deadline);
        if ((int32_t)(remaining - 1) > 0) break;
        if (event == head) rp_block(c, "event_sentinel_due_path_not_reconstructed", 0x953C);
        if (++dispatched > 1024) rp_block(c, "guest_event_dispatch_budget", 0x953C);

        const uint32_t previous = rp_u32(c, event + 4), next = rp_u32(c, event);
        const uint32_t callback = rp_u32(c, event + 12);
        rp_w32(c, c->gp + 0x1B0, 0);
        rp_w32(c, next + 4, previous);
        rp_w32(c, previous, next);
        rp_w32(c, event + 4, 0);
        rp_event(c, "milestone", "guest_event_due", callback, deadline);
        rp_pops_graphics_event(c, callback);
        now -= rp_u32(c, c->gp + 0x1B0);
    }

    const uint8_t *control_bytes = rp_memory(c, c->gp + 0x1C0, 2);
    const uint32_t control = control_bytes[0] | (uint32_t)control_bytes[1] << 8;
    const uint32_t status = rp_u32(c, c->gp + 0x130);
    if (control & 0x8000) {
        rp_pops_initialize_core(c);
    } else {
        rp_w32(c, c->gp + 0x1B0, remaining);
        if (control) {
            rp_w8(c, c->gp + 0x1C0, 0);
            rp_w8(c, c->gp + 0x1C1, 0);
            rp_pops_invalidate_ram_code(c);
        }
        if ((status & 1) && (status & rp_u32(c, c->gp + 0x134) & 0xFF00)) {
            const uint32_t pc = rp_u32(c, c->gp + 0x1A0);
            if (((pc >> 23) & 63) == 0 &&
                    (rp_u32(c, 0x09800000 | (pc & 0x1FFFFF)) >> 25) == 0x25)
                rp_block(c, "interrupt_GTE_completion_not_reconstructed", 0x9610);
            rp_w32(c, c->gp + 0x138, pc);
            rp_pops_prepare_exception(c, 0);
        }
    }
    rp_event(c, "milestone", "guest_events_rescheduled", rp_u32(c, c->gp + 0x1A0),
             rp_u32(c, c->gp + 0x1B0));
    return rp_u32(c, c->gp + 0x1B4);
}
