#ifndef REPOPS_POPS_SERIAL_H
#define REPOPS_POPS_SERIAL_H

#include "pops_dma.h"

/* Two 0x2c-byte serial/controller ports at core +0x2b0. */
typedef struct {
    rp_guest_event_layout event;
    uint16_t status;
    uint16_t control;
    uint8_t transfer_phase;
    uint8_t receive_data;
    uint8_t transmit_data;
    uint8_t device_kind;
    uint16_t bit_cycles;
    uint16_t protocol_step;
    uint32_t transfer_callback;
    uint32_t protocol_callback;
    uint32_t previous_sample_cycles;
    uint32_t sample_cycles;
} rp_serial_port_layout;

/* +0x3c00: selected bytes used by the standard digital-pad transaction. */
typedef struct {
    uint8_t response_bytes[0x20];
    uint8_t command;
    int8_t connected;
    uint8_t id_byte;
    uint8_t response_length;
    uint8_t configured_id;
    uint8_t extended_response_active;
    uint8_t extended_response_next;
    uint8_t unknown_27[5];
    uint8_t sync_byte;
    uint8_t unknown_2d[3];
} rp_controller_port_layout;

#define RP_SERIAL_PORT(c, index, member) \
    ((c)->gp + UINT32_C(0x2B0) + (uint32_t)(index) * (uint32_t)sizeof(rp_serial_port_layout) + \
     (uint32_t)offsetof(rp_serial_port_layout, member))
#define RP_CONTROLLER_PORT(c, index, member) \
    ((c)->gp + UINT32_C(0x3C00) + (uint32_t)(index) * (uint32_t)sizeof(rp_controller_port_layout) + \
     (uint32_t)offsetof(rp_controller_port_layout, member))

void rp_pops_serial_data_write(rp_context *, uint32_t address, uint32_t value);
void rp_pops_serial_control_write(rp_context *, uint32_t address, uint32_t value);
uint32_t rp_pops_serial_read(rp_context *, uint32_t address, uint32_t width);
void rp_pops_serial_event(rp_context *, uint32_t event, uint32_t callback);

_Static_assert(sizeof(rp_serial_port_layout) == 0x2C, "serial port stride");
_Static_assert(sizeof(rp_controller_port_layout) == 0x30, "controller port stride");
_Static_assert(offsetof(rp_serial_port_layout, status) == 0x10, "serial status");
_Static_assert(offsetof(rp_serial_port_layout, control) == 0x12, "serial control");
_Static_assert(offsetof(rp_serial_port_layout, bit_cycles) == 0x18, "serial bit timing");
_Static_assert(offsetof(rp_serial_port_layout, transfer_callback) == 0x1C, "serial transfer callback");

#endif
