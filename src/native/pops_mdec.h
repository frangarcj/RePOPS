#ifndef REPOPS_MDEC_H
#define REPOPS_MDEC_H
#include "pops_cdrom.h"

typedef struct {
    uint32_t command, remaining_bytes;
    uint32_t unknown_08[5];
    rp_guest_event_layout event;
} rp_mdec_stream_layout;

typedef struct {
    uint8_t earlier[0x400];
    uint32_t reset_parameters[7];
    uint8_t unknown_41c[0x620 - 0x41C];
    rp_mdec_stream_layout stream;
} rp_core_mdec_layout;

#define RP_MDEC_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_mdec_layout, member)
void rp_pops_mdec_reset(rp_context *);
uint32_t rp_pops_mdec_read(rp_context *, uint32_t address, uint32_t width);
void rp_pops_mdec_write(rp_context *, uint32_t address, uint32_t value);

_Static_assert(sizeof(rp_mdec_stream_layout) == 0x2C, "MDEC reset range");
_Static_assert(offsetof(rp_core_mdec_layout, stream) == 0x620, "MDEC command state");
_Static_assert(offsetof(rp_core_mdec_layout, stream.event) == 0x63C, "MDEC pending event");
#endif
