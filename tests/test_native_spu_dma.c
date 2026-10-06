#include "../src/native/pops_spu_dma.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned scheduled;
static uint32_t scheduled_delay;
void rp_pops_schedule_event(rp_context *c, uint32_t node, uint32_t delay)
{
    assert(node == RP_SPU_DMA_ADDRESS(c, deferred_event));
    ++scheduled; scheduled_delay = delay;
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0x09800000, 0x600000, calloc(1, 0x600000)};
    c->regions[1] = (rp_region){0x09F40000, sizeof(rp_me_shared_layout), calloc(1, sizeof(rp_me_shared_layout))};
    assert(c->regions[0].bytes && c->regions[1].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    const uint32_t samples = RP_SHARED_ADDRESS(sample_ram);
    uint8_t payload[32];
    for (unsigned i = 0; i < 32; ++i) payload[i] = (uint8_t)(i * 7 + 3);
    memcpy(rp_memory(c, 0x09800400, 32), payload, 32);
    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), 8);
    rp_cd_w16(c, RP_SHARED_ADDRESS(irq_address_units), 2);
    rp_cd_w16(c, RP_SHARED_ADDRESS(control), 0x40);
    assert(rp_pops_spu_dma_transfer(c, 0x400, 32, 1) == 32);
    assert(!memcmp(rp_memory(c, samples + 16, 32), payload, 32));
    assert(rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)) == 24);
    assert(rp_cd_u8(c, RP_SPU_DMA_ADDRESS(c, transfer_irq_latch)) == 0x40);

    /* The read IRQ interval extends 32 bytes past the copied data. */
    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), 8);
    rp_w8(c, RP_SPU_DMA_ADDRESS(c, transfer_irq_latch), 0);
    rp_cd_w16(c, RP_SHARED_ADDRESS(irq_address_units), 8);
    memset(rp_memory(c, 0x09C004FC, 40), 0xA5, 40);
    assert(rp_pops_spu_dma_transfer(c, 0x500, 32, 0) == 32);
    assert(!memcmp(rp_memory(c, 0x09800500, 32), payload, 32));
    for (unsigned i = 0; i < 32; ++i) assert(!rp_cd_u8(c, 0x09C00500 + i));
    assert(rp_u32(c, 0x09C004FC) == 0xA5A5A5A5 && rp_u32(c, 0x09C00520) == 0xA5A5A5A5);
    assert(rp_cd_u8(c, RP_SPU_DMA_ADDRESS(c, transfer_irq_latch)) == 0x40);

    /* Only the tail span is copied; return and wrapped cursor use all bytes. */
    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), 0x3FFFC);
    memset(rp_memory(c, samples, 8), 0xAA, 8);
    assert(rp_pops_spu_dma_transfer(c, 0x400, 16, 1) == 16);
    assert(!memcmp(rp_memory(c, samples + 0x7FFF8, 8), payload, 8));
    assert(rp_u32(c, samples) == 0xAAAAAAAA);
    assert(rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)) == 4);
    assert(rp_pops_spu_dma_transfer(c, 0x400, 0, 1) == 1);
    assert(rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)) == 4);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 0x200);
    assert(rp_pops_spu_dma_transfer(c, 0x400, 16, 1) == 1);

    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), 0);
    rp_cd_w16(c, RP_SPU_DMA_ADDRESS(c, defer_write_compatibility), 1);
    memset(rp_memory(c, samples, 16), 0, 16);
    assert(rp_pops_spu_dma_transfer(c, 0x400, 16, 1) == 16);
    assert(scheduled == 1 && scheduled_delay == 64);
    assert(rp_u32(c, RP_SPU_DMA_ADDRESS(c, deferred_address)) == 0x80000400);
    assert(!rp_u32(c, samples));
    assert(!rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)));
    rp_pops_spu_dma_event(c);
    assert(scheduled == 1);
    assert(!memcmp(rp_memory(c, samples, 16), payload, 16));
    assert(rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)) == 8);

    rp_cd_w16(c, RP_SPU_DMA_ADDRESS(c, defer_write_compatibility), 0);
    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), 0);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 0);
    memset(rp_memory(c, 0x09801000, 0x1004), 0x5A, 0x1004);
    assert(rp_pops_spu_dma_transfer(c, 0x1000, 0x1004, 1) == 0x1004);
    assert(!memcmp(rp_memory(c, samples, 0x1004), rp_memory(c, 0x09801000, 0x1004), 0x1004));
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("SPU DMA: both directions, IRQ window, truncated tail, deferred copy and return values passed.");
    return 0;
}
