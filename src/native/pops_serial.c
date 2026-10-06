#include "pops_serial.h"

static uint32_t serial_port(rp_context *c, uint32_t address)
{
    return RP_SERIAL_PORT(c, (address >> 4) & 1, event);
}

static uint32_t serial_shadow(rp_context *c, uint32_t address)
{
    return c->gp + 0x2000 + (address & 0xFFF);
}

void rp_pops_serial_data_write(rp_context *c, uint32_t address, uint32_t value)
{
    rp_function(c, 0xA06C, "pops.write_serial_data");
    if (address & 0xF) return;
    const unsigned port = (address >> 4) & 1;
    const uint32_t node = serial_port(c, address);
    rp_cd_w16(c, RP_SERIAL_PORT(c, port, status),
               rp_cd_u16(c, RP_SERIAL_PORT(c, port, status)) & 0xFFFA);
    rp_w8(c, RP_SERIAL_PORT(c, port, transmit_data), (uint8_t)value);
    rp_event(c, "serial_write", "transmit_data", address, (uint8_t)value);
    if (rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev)))
        rp_pops_remove_event(c, node);
    rp_w32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, callback),
           rp_u32(c, RP_SERIAL_PORT(c, port, transfer_callback)));
    rp_pops_schedule_event(c, node, rp_cd_u16(c, RP_SERIAL_PORT(c, port, bit_cycles)));
}

void rp_pops_serial_control_write(rp_context *c, uint32_t address, uint32_t value)
{
    rp_function(c, 0xA0E8, "pops.write_serial_control");
    const unsigned port = (address >> 4) & 1;
    const uint32_t node = serial_port(c, address);
    const uint32_t shadow = serial_shadow(c, address);
    if ((address & 0xF) == 0xA) {
        if (value & 0x40) {
            rp_w8(c, RP_SERIAL_PORT(c, port, transfer_phase), 0);
            if (!port) {
                rp_cd_w16(c, RP_SERIAL_PORT(c, port, status), 5);
            }
            if (rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev)))
                rp_pops_remove_event(c, node);
            value = 0;
        } else {
            const uint32_t old = rp_cd_u16(c, RP_SERIAL_PORT(c, port, control));
            const uint32_t newly_set = value & ~old;
            if (!port && (value & 0x10)) {
                rp_cd_w16(c, RP_SERIAL_PORT(c, port, status),
                           rp_cd_u16(c, RP_SERIAL_PORT(c, port, status)) & 0xFDC7);
                value -= 0x10;
            }
            if (newly_set & 2) {
                uint32_t debit = rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit));
                if ((int32_t)debit > 100) debit = 100;
                rp_w32(c, RP_DMA_ADDRESS(c, deferred_frame_debit),
                       rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit)) - debit);
                rp_core_set_downcount(c, rp_core_downcount(c) - debit);
                rp_w8(c, RP_SERIAL_PORT(c, port, transfer_phase), 0);
            }
        }
        rp_cd_w16(c, RP_SERIAL_PORT(c, port, control), (uint16_t)value);
        rp_cd_w16(c, shadow, (uint16_t)value);
        return;
    }

    const uint16_t old = rp_cd_u16(c, shadow);
    if (old == value) return;
    rp_cd_w16(c, shadow, (uint16_t)value);
    const uint32_t block = shadow & ~UINT32_C(0xF);
    const uint32_t mode = rp_cd_u16(c, block + 8);
    const uint32_t baud = rp_cd_u16(c, block + 0xE);
    const uint32_t clocks = ((mode >> 2) & 3) + ((mode >> 4) & 1) + 5;
    const uint32_t shift = (48u >> (mode & 3)) & 7;
    rp_cd_w16(c, RP_SERIAL_PORT(c, port, bit_cycles),
               (uint16_t)((baud * clocks) << shift));
}

static uint32_t controller_protocol(rp_context *c, unsigned slot,
                                    uint32_t phase, uint32_t transmit)
{
    rp_function(c, 0xA250, "pops.controller_serial_protocol_partial");
    if (slot >= 2) rp_block(c, "controller_slot_out_of_range", slot);
    if ((int8_t)rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, connected)) <= 0)
        return 0x1FF;
    if (phase == 0) {
        /* Explicit neutral-input adapter for +0xA314..+0xA390; the firmware
         * stores a fresh button halfword here on every poll. */
        ++c->services;
        rp_event(c, "headless_adapter", "controller_neutral_buttons", slot, 0xFFFF);
        rp_cd_w16(c, RP_CONTROLLER_PORT(c, slot, response_bytes), 0xFFFF);
        return 0xFF;
    }
    if (phase == 1) {
        const uint32_t result = rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, id_byte));
        rp_w8(c, RP_CONTROLLER_PORT(c, slot, command), (uint8_t)transmit);
        rp_w8(c, RP_CONTROLLER_PORT(c, slot, extended_response_active),
              rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, extended_response_next)));
        return result;
    }
    if (phase == 2)
        return rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, sync_byte));

    const uint32_t command = rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, command));
    if (command != 0x42) return 0x1FF;
    const uint32_t offset = phase - 3;
    const uint32_t configured = rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, response_length));
    const uint32_t length = rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, extended_response_active)) ?
                            6 : configured;
    if (offset < length)
        return rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, response_bytes[offset]));
    return rp_cd_u8(c, RP_CONTROLLER_PORT(c, slot, response_bytes[configured])) | 0x100;
}

void rp_pops_serial_event(rp_context *c, uint32_t event, uint32_t callback)
{
    const uint32_t first = RP_SERIAL_PORT(c, 0, event);
    if (event < first || event >= first + 2 * sizeof(rp_serial_port_layout) ||
        (event - first) % sizeof(rp_serial_port_layout))
        rp_block(c, "serial_event_node_not_reconstructed", event);
    const unsigned port = (event - first) / sizeof(rp_serial_port_layout);
    if (callback == 0x1A56C) {
        rp_function(c, 0x1A56C, "pops.serial_secondary_noop");
        return;
    }
    if (callback == 0xA220) {
        rp_function(c, 0xA220, "pops.serial_interrupt_event");
        if (rp_cd_u16(c, RP_SERIAL_PORT(c, port, control)) & 0x1000)
            rp_pops_raise_irq(c, (uint32_t)rp_cd_u8(c, RP_SERIAL_PORT(c, port, device_kind)) << 7);
        return;
    }
    if (callback != 0x9E64) {
        rp_event(c, "serial_boundary", "serial_event_callback", event, callback);
        rp_block(c, "serial_transfer_callback_not_reconstructed", callback);
    }

    rp_function(c, 0x9E64, "pops.serial_transfer_callback_partial");
    uint32_t phase = rp_cd_u8(c, RP_SERIAL_PORT(c, port, transfer_phase));
    const uint32_t transmit = rp_cd_u8(c, RP_SERIAL_PORT(c, port, transmit_data));
    if (!phase) {
        if (transmit == 1) {
            rp_w32(c, RP_SERIAL_PORT(c, port, protocol_callback), 0xA250);
            rp_cd_w16(c, RP_SERIAL_PORT(c, port, protocol_step), 0x43);
        } else if (transmit == 0x81) {
            rp_w32(c, RP_SERIAL_PORT(c, port, protocol_callback), 0xA508);
            rp_cd_w16(c, RP_SERIAL_PORT(c, port, protocol_step), 0x10E);
        } else {
            rp_w32(c, RP_SERIAL_PORT(c, port, protocol_callback), 0x1A574);
            rp_cd_w16(c, RP_SERIAL_PORT(c, port, protocol_step), 0x43);
        }
    }
    const uint32_t protocol = rp_u32(c, RP_SERIAL_PORT(c, port, protocol_callback));
    uint32_t result;
    if (protocol == 0xA250) {
        result = controller_protocol(c,
            (rp_cd_u16(c, RP_SERIAL_PORT(c, port, control)) >> 13) & 1,
            phase, transmit);
    } else if (protocol == 0x1A574) {
        rp_function(c, 0x1A574, "pops.serial_protocol_terminate");
        result = 0x1FF;
    } else {
        rp_event(c, "serial_boundary", "serial_protocol_callback", protocol, transmit);
        rp_block(c, protocol == 0xA508 ?
                 "memory_card_serial_protocol_not_reconstructed" :
                 "serial_protocol_callback_not_reconstructed", protocol);
    }
    rp_w8(c, RP_SERIAL_PORT(c, port, receive_data), (uint8_t)result);
    rp_cd_w16(c, RP_SERIAL_PORT(c, port, status),
              rp_cd_u16(c, RP_SERIAL_PORT(c, port, status)) | 7);
    if (result & 0x100) {
        rp_w32(c, RP_SERIAL_PORT(c, port, protocol_callback), 0x1A574);
        return;
    }
    rp_w32(c, RP_FIELD_ADDRESS(event, rp_guest_event_layout, callback), 0xA220);
    rp_pops_schedule_event(c, event, rp_cd_u16(c, RP_SERIAL_PORT(c, port, protocol_step)));
    rp_w8(c, RP_SERIAL_PORT(c, port, transfer_phase), (uint8_t)(phase + 1));
}

uint32_t rp_pops_serial_read(rp_context *c, uint32_t address, uint32_t width)
{
    rp_function(c, 0x9F30, "pops.read_serial_register");
    const unsigned port = (address >> 4) & 1;
    const uint32_t node = serial_port(c, address);
    const uint32_t offset = address & 0xF;
    if (offset == 0) {
        if (rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev))) {
            const uint32_t now = rp_core_guest_cycles(c);
            const uint32_t deadline = rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, deadline_cycles));
            const int32_t remaining = (int32_t)(deadline - now);
            if (remaining < 34) {
                if (remaining > 0) {
                    rp_core_set_downcount(c, rp_core_downcount(c) - (uint32_t)remaining);
                    const uint32_t debit = rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit));
                    const uint32_t difference = debit - (uint32_t)remaining;
                    rp_w32(c, RP_DMA_ADDRESS(c, deferred_frame_debit),
                           (int32_t)difference > 0 ? difference : 0);
                }
                const uint32_t callback = rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, callback));
                rp_pops_remove_event(c, node);
                rp_pops_serial_event(c, node, callback);
            }
        }
        const uint8_t data = rp_cd_u8(c, RP_SERIAL_PORT(c, port, receive_data));
        rp_cd_w16(c, RP_SERIAL_PORT(c, port, status),
                   rp_cd_u16(c, RP_SERIAL_PORT(c, port, status)) & ~UINT16_C(2));
        return width ? data : (uint32_t)(int32_t)(int8_t)data;
    }
    if (offset != 4) return 0;

    const uint32_t now = rp_core_guest_cycles(c);
    const uint32_t old_sample = rp_u32(c, RP_SERIAL_PORT(c, port, previous_sample_cycles));
    if (now - old_sample < 80 &&
        rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev))) {
        const uint32_t downcount = rp_core_downcount(c);
        if ((int32_t)downcount > 0) {
            const uint32_t debit = rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit));
            const uint32_t difference = debit - downcount;
            rp_w32(c, RP_DMA_ADDRESS(c, deferred_frame_debit),
                   (int32_t)difference > 0 ? difference : 0);
            rp_core_set_downcount(c, 0);
        }
        const uint32_t callback = rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, callback));
        rp_pops_remove_event(c, node);
        rp_pops_serial_event(c, node, callback);
    }
    const uint32_t previous = rp_u32(c, RP_SERIAL_PORT(c, port, sample_cycles));
    rp_w32(c, RP_SERIAL_PORT(c, port, previous_sample_cycles), previous);
    rp_w32(c, RP_SERIAL_PORT(c, port, sample_cycles), now);
    return rp_cd_u16(c, RP_SERIAL_PORT(c, port, status));
}
