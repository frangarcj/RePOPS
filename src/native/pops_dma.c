#include "pops_dma.h"

/* +0x9158..+0x91BB and its width table at +0xD45AC. The helper refunds
 * four cycles before reading the shadow, including the default LHU path. */
uint32_t rp_pops_dma_read(rp_context *c, uint32_t address, uint32_t width)
{
    rp_function(c, 0x9158, "pops.read_DMA_register");
    const uint32_t shadow = RP_DMA_ADDRESS(c, registers) + (address & 0x7F);
    rp_core_set_downcount(c, rp_core_downcount(c) + 4);
    uint32_t value;
    switch (width) {
    case 0:
        value = rp_cd_u8(c, shadow);
        if (value & 0x80) value |= UINT32_C(0xFFFFFF00);
        break;
    case 1:
        value = rp_cd_u16(c, shadow);
        if (value & 0x8000) value |= UINT32_C(0xFFFF0000);
        break;
    case 2: value = rp_u32(c, shadow); break;
    case 4: value = rp_cd_u8(c, shadow); break;
    default: value = rp_cd_u16(c, shadow); break;
    }
    rp_event(c, "DMA_register_read", "shadow_with_original_cycle_refund", address, value);
    return value;
}

/* +0x8BB8: postpone active channels matching the original mode mask. */
static uint32_t delay_active(rp_context *c, uint16_t mask, uint32_t delay, uint32_t horizon)
{
    rp_function(c, 0x8BB8, "pops.delay_active_DMA_events");
    uint32_t total = 0;
    for (int channel = 4; channel >= 0; --channel) {
        const uint32_t node = RP_DMA_ADDRESS(c, channels[channel]);
        const uint32_t prev = rp_u32(c, RP_DMA_CHANNEL(c, channel, event.prev));
        if (!prev || !(rp_cd_u16(c, RP_DMA_CHANNEL(c, channel, transfer_mode)) & mask)) continue;
        uint32_t extra = delay;
        if (channel == 0) {
            extra -= 0x1800;
            if ((int32_t)extra <= 0) return total;
        }
        const uint32_t remaining = rp_u32(c, RP_DMA_CHANNEL(c, channel, event.deadline_cycles)) -
                                   rp_core_guest_cycles(c);
        if ((int32_t)remaining <= 0) continue;
        if (remaining < horizon) extra = (delay * remaining) / horizon;
        total += extra;
        const uint32_t next = rp_u32(c, RP_DMA_CHANNEL(c, channel, event.next));
        /* Original inline unlink does not adjust the core clock. */
        rp_w32(c, RP_FIELD_ADDRESS(next, rp_guest_event_layout, prev), prev);
        rp_w32(c, RP_FIELD_ADDRESS(prev, rp_guest_event_layout, next), next);
        rp_pops_schedule_event(c, node, remaining + extra);
    }
    return total;
}

/* +0x8B1C completion; queued-channel arbitration is a separate boundary. */
void rp_pops_dma_finish(rp_context *c, uint32_t node)
{
    rp_function(c, 0x8B1C, "pops.complete_DMA_channel_partial");
    const unsigned channel = rp_cd_u16(c, RP_FIELD_ADDRESS(node, rp_dma_channel_layout, channel));
    if (channel >= 7) rp_block(c, "DMA_channel_out_of_range", channel);
    rp_core_set_downcount(c, rp_core_downcount(c) -
        rp_u32(c, RP_FIELD_ADDRESS(node, rp_dma_channel_layout, completion_debit)));
    const uint32_t control = RP_DMA_REGISTER(c, channel, channel_control);
    rp_w32(c, control, rp_u32(c, control) & ~UINT32_C(0x01000000));
    rp_w32(c, RP_DMA_REGISTER(c, channel, block_control), 0);
    uint32_t irq = rp_u32(c, RP_DMA_ADDRESS(c, interrupt_control));
    if ((irq & 0x00800000) && (irq & (1u << (channel + 16)))) {
        irq |= UINT32_C(0x80000000) | (1u << (channel + 24));
        rp_w32(c, RP_DMA_ADDRESS(c, interrupt_control), irq);
        rp_pops_raise_irq(c, 8);
    }
    rp_event(c, "milestone", "DMA_channel_completed", channel, irq);
    if (rp_cd_u16(c, RP_DMA_ADDRESS(c, pending_channels)))
        rp_block(c, "DMA_pending_arbitration_not_reconstructed", 0x8D88);
}

/* +0x8E4C: enabled-channel selection and the reached CD transfer route. */
void rp_pops_dma_try_channel(rp_context *c, unsigned channel)
{
    rp_function(c, 0x8E4C, "pops.try_DMA_channel_partial");
    if (channel >= 7) rp_block(c, "DMA_channel_out_of_range", channel);
    if (rp_u32(c, RP_DMA_CHANNEL(c, channel, event.prev))) return;
    const uint32_t chcr = rp_u32(c, RP_DMA_REGISTER(c, channel, channel_control));
    const uint32_t dpcr = rp_u32(c, RP_DMA_ADDRESS(c, priority));
    if (!(chcr & 0x01000000) || !(dpcr & (8u << (channel * 4)))) return;
    const unsigned priority = (dpcr >> (channel * 4)) & 7;
    int ahead = ((dpcr >> 28) & 7) < priority ? 0 : -1;
    for (int other = 4; other >= 0; --other) {
        if (!rp_u32(c, RP_DMA_CHANNEL(c, other, event.prev))) continue;
        const uint32_t other_control = rp_u32(c, RP_DMA_REGISTER(c, other, channel_control));
        bool postpone = (other_control & 0x100) != 0;
        if (!postpone && (rp_cd_u16(c, RP_DMA_CHANNEL(c, other, transfer_mode)) & 2)) {
            ahead += ((dpcr >> (other * 4)) & 7) < priority;
            postpone = ahead > 0;
        }
        if (postpone) {
            const uint32_t pending = RP_DMA_ADDRESS(c, pending_channels);
            rp_cd_w16(c, pending, rp_cd_u16(c, pending) | (uint16_t)(1u << channel));
            return;
        }
    }
    const unsigned mode = (chcr >> 8) & 7;
    rp_w32(c, RP_DMA_CHANNEL(c, channel, completion_debit), 0);
    uint32_t bytes = 0;
    if (mode != 4) {
        const uint32_t block = rp_u32(c, RP_DMA_REGISTER(c, channel, block_control));
        uint32_t words = block & 0xFFFF;
        if (!words) words = 0x10000;
        if (mode >= 2) {
            if (mode != 2) return;
            words *= block >> 16;
        }
        bytes = words << 2;
    }
    rp_cd_w16(c, RP_DMA_CHANNEL(c, channel, transfer_mode), (uint16_t)mode);
    const uint32_t node = RP_DMA_ADDRESS(c, channels[channel]);
    const uint32_t madr = RP_DMA_REGISTER(c, channel, address);
    const uint32_t address = rp_u32(c, madr);
    if (address & 0x800000) { rp_pops_dma_finish(c, node); return; }
    const uint32_t callback = rp_u32(c, RP_DMA_CHANNEL(c, channel, transfer_callback));
    if (callback != 0xCE18)
        rp_block(c, "DMA_transfer_callback_not_reconstructed", callback);
    const uint32_t moved = rp_pops_cd_dma_transfer(c, address & 0xFFFFFC, bytes, chcr);
    if ((int32_t)moved <= 0) {
        if (moved) rp_w32(c, madr, moved & 0xFFFFFC);
        else if (channel < 2) rp_cd_w16(c, RP_DMA_CHANNEL(c, channel, transfer_mode), 0);
        return;
    }
    const uint32_t words = moved >> 2;
    const unsigned cycle_shift = (UINT32_C(0x01035000) >> (channel * 4)) & ((chcr & 1) ^ 7);
    uint32_t cost = words << cycle_shift;
    rp_w32(c, madr, address + ((chcr & 2) ? 0u - (words << 2) : words << 2));
    if (!mode) {
        cost += 3;
        (void)delay_active(c, 7, cost, 0);
        rp_core_set_downcount(c, rp_core_downcount(c) - cost);
        rp_pops_dma_finish(c, node);
    } else {
        uint32_t delay;
        if (mode == 1) {
            delay = ((words - 1) >> ((chcr >> 16) & 7)) << ((chcr >> 20) & 7);
            delay += moved * 3;
            (void)delay_active(c, 7, delay, 0);
            cost = 0;
        } else if (mode == 2) {
            const uint32_t blocks = rp_u32(c, RP_DMA_REGISTER(c, channel, block_control)) >> 16;
            cost += blocks * 2;
            const uint32_t horizon = cost + blocks * 2;
            delay = horizon + delay_active(c, 2, cost, horizon);
            if (((dpcr >> 28) & 7) < priority) {
                cost -= blocks * 4; delay += blocks * 4;
                if ((int32_t)cost < 0) cost = 0;
            }
        } else { delay = moved; cost = 0; }
        rp_pops_schedule_event(c, node, delay);
        rp_w32(c, RP_DMA_CHANNEL(c, channel, completion_debit), cost >> 1);
        rp_core_set_downcount(c, rp_core_downcount(c) - (cost >> 1));
    }
}

/* +0x92A4. Partial-width writes use the ordinary shadow-store helper. */
void rp_pops_dma_channel_write(rp_context *c, uint32_t address, uint32_t value, uint32_t width)
{
    rp_function(c, 0x92A4, "pops.write_DMA_channel_partial");
    if ((int32_t)width < 2) { rp_pops_shadow_write(c, address, value, width); return; }
    const unsigned channel = (address >> 4) & 7;
    if (channel >= 7) rp_block(c, "DMA_channel_out_of_range", channel);
    const uint32_t target = RP_DMA_ADDRESS(c, registers) + (address & 0x7F);
    rp_w32(c, target, value);
    rp_event(c, "DMA_register_write", "channel_control_or_padding", address, value);
    if (address & 7) return;
    if (value & 0x01000000) { rp_pops_dma_try_channel(c, channel); return; }
    const uint32_t node = RP_DMA_ADDRESS(c, channels[channel]);
    if (rp_u32(c, RP_DMA_CHANNEL(c, channel, event.prev))) {
        if (channel == 2) rp_block(c, "GPU_DMA_cancel_not_reconstructed", 0x9340);
        rp_pops_remove_event(c, node);
    }
    if (rp_cd_u16(c, RP_DMA_ADDRESS(c, pending_channels)))
        rp_block(c, "DMA_pending_arbitration_not_reconstructed", 0x8D88);
}
