#ifndef REPOPS_DMA_H
#define REPOPS_DMA_H
#include "pops_cdrom.h"

typedef struct {
    rp_guest_event_layout event;
    uint32_t transfer_callback, completion_debit;
    uint16_t transfer_mode, channel;
} rp_dma_channel_layout;
typedef struct { uint32_t address, block_control, channel_control, reserved; } rp_dma_registers_layout;
typedef struct {
    uint8_t earlier_state[0x1D8];
    uint32_t compiled_ram_pages;
    uint8_t unknown_1dc[8];
    uint32_t deferred_frame_debit;
    rp_dma_channel_layout channels[7];
    uint16_t pending_channels;
    uint8_t unknown_2ae[0x2080 - 0x2AE];
    rp_dma_registers_layout registers[7];
    uint32_t priority, interrupt_control;
} rp_core_dma_layout;

#define RP_DMA_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_dma_layout, member)
#define RP_DMA_CHANNEL(c, index, member) RP_DMA_ADDRESS(c, channels[index].member)
#define RP_DMA_REGISTER(c, index, member) RP_DMA_ADDRESS(c, registers[index].member)

void rp_pops_dma_try_channel(rp_context *, unsigned);
uint32_t rp_pops_dma_delay_active(rp_context *, uint16_t mask, uint32_t delay, uint32_t horizon);
uint32_t rp_pops_dma_read(rp_context *, uint32_t address, uint32_t width);
void rp_pops_dma_channel_write(rp_context *, uint32_t, uint32_t, uint32_t);
void rp_pops_dma_finish(rp_context *, uint32_t);
uint32_t rp_pops_cd_dma_transfer(rp_context *, uint32_t, uint32_t, uint32_t);

_Static_assert(sizeof(rp_dma_channel_layout) == 0x1C, "DMA channel state stride");
_Static_assert(sizeof(rp_dma_registers_layout) == 0x10, "DMA register stride");
_Static_assert(offsetof(rp_core_dma_layout, channels) == 0x1E8, "DMA core state");
_Static_assert(offsetof(rp_core_dma_layout, deferred_frame_debit) == 0x1E4, "DMA frame debit");
_Static_assert(offsetof(rp_core_dma_layout, pending_channels) == 0x2AC, "DMA pending mask");
_Static_assert(offsetof(rp_core_dma_layout, priority) == 0x20F0, "DPCR shadow");
#endif
