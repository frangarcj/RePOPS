#ifndef REPOPS_MEMORY_CARD_H
#define REPOPS_MEMORY_CARD_H
#include "pops_cdrom.h"

enum {
    RP_MC_GROUP_BASE = 0x10CB50,
    RP_MC_SECTOR_BYTES = 128,
    RP_MC_SECTOR_COUNT = 1024,
    RP_MC_STATE_QUERY = 0x14D07C
};

/* +0xA508 indexes this same backing as the volatile-card worker. */
typedef struct {
    uint8_t flags, checksum;
    uint16_t sector;
    uint8_t command, status;
    uint16_t reserved;
    uint8_t container_header[0x80];
    uint8_t sectors[RP_MC_SECTOR_COUNT][RP_MC_SECTOR_BYTES];
} rp_memory_card_layout;

#define RP_MC_SLOT(slot, member) \
    RP_FIELD_ADDRESS(RP_MC_GROUP_BASE + (uint32_t)(slot) * sizeof(rp_memory_card_layout), \
                     rp_memory_card_layout, member)

uint32_t rp_pops_mc_serial(rp_context *, uint32_t phase, uint32_t slot, uint32_t transmit);

_Static_assert(sizeof(rp_memory_card_layout) == 0x20088, "memory card slot stride");
_Static_assert(offsetof(rp_memory_card_layout, sector) == 2, "serial sector address");
_Static_assert(offsetof(rp_memory_card_layout, sectors) == 0x88, "raw card data offset");
#endif
