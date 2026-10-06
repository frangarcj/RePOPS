#include "pops_spu_dma.h"
#include <string.h>

/* +0x8698. The original clamps the copied span at the end of sample RAM,
 * while advancing its cursor by the entire request and returning that request
 * (or 1 under the compatibility flag). Do not silently add a second copy. */
uint32_t rp_pops_spu_dma_transfer(rp_context *c, uint32_t address, uint32_t bytes, uint32_t control)
{
    rp_function(c, 0x8698, "pops.SPU_DMA_transfer");
    const uint32_t ram = 0x09800000 | (address & 0x1FFFFF);
    const uint32_t offset = rp_u32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index)) << 1;
    if (!bytes) return 1;
    const bool write = (control & 1) != 0;
    const int16_t deferred = (int16_t)rp_cd_u16(c, RP_SPU_DMA_ADDRESS(c, defer_write_compatibility));
    if (deferred > 0 && write && !(address & UINT32_C(0x80000000))) {
        rp_pops_schedule_event(c, RP_SPU_DMA_ADDRESS(c, deferred_event), bytes << 2);
        rp_w32(c, RP_SPU_DMA_ADDRESS(c, deferred_address), address | UINT32_C(0x80000000));
        rp_w32(c, RP_SPU_DMA_ADDRESS(c, deferred_bytes), bytes);
        rp_event(c, "spu_dma", "write_deferred_without_copy", address, bytes);
        return bytes;
    }
    const uint32_t end = offset + bytes;
    if (offset >= 0x80000 || end < offset)
        rp_block(c, "SPU_DMA_cursor_domain_not_reconstructed", offset);
    const uint32_t copied = end > 0x80000 ? 0x80000 - offset : bytes;
    rp_w32(c, RP_SPU_DMA_ADDRESS(c, transfer_halfword_index), (end >> 1) & 0x3FFFF);
    const uint32_t irq = (uint32_t)rp_cd_u16(c, RP_SHARED_ADDRESS(irq_address_units)) << 3;
    if ((rp_cd_u16(c, RP_SHARED_ADDRESS(control)) & 0x40) &&
        irq >= offset && irq < end + (write ? 0 : 32))
        rp_w8(c, RP_SPU_DMA_ADDRESS(c, transfer_irq_latch), 0x40);

    const uint32_t sample = RP_SHARED_ADDRESS(sample_ram) + offset;
    if (!write) {
        /* Original RAM-code validity words are cleared before readback. Cache
         * operations themselves are a coherent-host adapter, not PSP cache. */
        if (copied & 3) rp_block(c, "SPU_DMA_read_tag_alignment_not_reconstructed", copied);
        memset(rp_memory(c, ram + 0x400000, copied), 0, copied);
    }
    const uint32_t source = write ? ram : sample, destination = write ? sample : ram;
    ++c->services;
    rp_event(c, "host_adapter", copied > 0x1000 ? "sceDmacMemcpy_coherent_host" :
             "Kernel_Library_1839852A_memcpy_host", destination, copied);
    memcpy(rp_memory(c, destination, copied), rp_memory(c, source, copied), copied);
    rp_event(c, "spu_dma", write ? "sample_RAM_written" : "sample_RAM_read", offset, copied);
    return (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & 0x200) ? 1 : bytes;
}

/* +0x8898 resumes with the tagged address so the same transfer cannot defer
 * itself again. The original ignores its returned byte count. */
void rp_pops_spu_dma_event(rp_context *c)
{
    rp_function(c, 0x8898, "pops.resume_deferred_SPU_DMA");
    (void)rp_pops_spu_dma_transfer(c,
        rp_u32(c, RP_SPU_DMA_ADDRESS(c, deferred_address)),
        rp_u32(c, RP_SPU_DMA_ADDRESS(c, deferred_bytes)), 1);
}
