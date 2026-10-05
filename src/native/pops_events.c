#include "runtime.h"

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
