#ifndef REPOPS_SPU_DMA_H
#define REPOPS_SPU_DMA_H
#include "pops_cdrom.h"

typedef struct {
    uint8_t earlier[0x34A];
    uint8_t transfer_irq_latch, unknown_34b;
    uint32_t transfer_halfword_index;
    uint8_t unknown_350[0x360 - 0x350];
    rp_guest_event_layout deferred_event;
    uint32_t deferred_address, deferred_bytes;
    uint8_t unknown_378[0x702 - 0x378];
    int16_t defer_write_compatibility;
} rp_core_spu_dma_layout;

#define RP_SPU_DMA_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_spu_dma_layout, member)
uint32_t rp_pops_spu_dma_transfer(rp_context *, uint32_t address, uint32_t bytes, uint32_t control);
void rp_pops_spu_dma_event(rp_context *);

_Static_assert(offsetof(rp_core_spu_dma_layout, transfer_halfword_index) == 0x34C, "SPU transfer cursor");
_Static_assert(offsetof(rp_core_spu_dma_layout, deferred_event) == 0x360, "SPU DMA event");
_Static_assert(offsetof(rp_core_spu_dma_layout, defer_write_compatibility) == 0x702, "SPU deferred write flag");
#endif
