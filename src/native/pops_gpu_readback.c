#include "pops_gpu.h"
#include <string.h>

#define GPU32(member) rp_u32(c, RP_GPU_ADDRESS(c, member))
#define GPU16(member) rp_cd_u16(c, RP_GPU_ADDRESS(c, member))
#define GPU8(member) rp_cd_u8(c, RP_GPU_ADDRESS(c, member))
#define SET_GPU32(member, value) rp_w32(c, RP_GPU_ADDRESS(c, member), (uint32_t)(value))
#define SET_GPU16(member, value) rp_cd_w16(c, RP_GPU_ADDRESS(c, member), (uint16_t)(value))
#define SET_GPU8(member, value) rp_w8(c, RP_GPU_ADDRESS(c, member), (uint8_t)(value))

static uint32_t emit(rp_context *c, uint32_t out, uint32_t word)
{
    rp_w32(c, out, word);
    rp_event(c, "GPU_GE_word", "readback_emitted_not_executed", out, word);
    return out + 4;
}

/* +0x13224 and the eight-entry jump table at +0xD53BC. Selectors outside
 * the table are themselves the value, including 0xFF after a transfer. */
static uint32_t query_value(rp_context *c, uint32_t selector)
{
    switch (selector) {
    case 2: return GPU32(texture_window);
    case 3:
        return (uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[0]) |
               ((uint32_t)(int32_t)(int16_t)GPU16(draw_area_start[1]) << 10);
    case 4:
        return (uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[0]) |
               ((uint32_t)(int32_t)(int16_t)GPU16(draw_area_end[1]) << 10);
    case 5:
        return (GPU16(drawing_offset[0]) & 0x7FF) |
               ((uint32_t)(GPU16(drawing_offset[1]) & 0x7FF) << 11);
    case 7: return 0;
    default: return selector;
    }
}

/* +0x1320C: any words beyond a completed CPU transfer repeat its last packed
 * destination word. An odd final pixel preserves that word's old upper half. */
static void fill_tail(rp_context *c, uint32_t destination, uint32_t end, uint32_t value)
{
    for (; destination != end; destination += 4) rp_w32(c, destination, value);
    SET_GPU32(transfer_read_latch, value);
}

/* +0x130BC is a buffer writer, not a value-returning GPUREAD function. The
 * scalar port supplies data_read_latch and length 4; DMA supplies guest RAM.
 * GE services must finish actual writes before returning to either path. */
void rp_pops_gpu_read_data(rp_context *c, uint32_t destination, uint32_t bytes)
{
    rp_function(c, 0x130BC, "pops.read_GPU_data_buffer");
    if ((destination & 3) || !bytes || (bytes & 3) || bytes > UINT32_MAX - destination)
        rp_block(c, "GPU_readback_buffer_domain_not_reconstructed", destination);
    const uint32_t end = destination + bytes;
    uint32_t selector = GPU8(read_selector);
    uint32_t next = destination;
    if (selector == RP_GPU_READ_START) {
        uint32_t out = GPU32(list_cursor);
        if ((int8_t)GPU8(ge_transfer_pending) < 0) {
            SET_GPU8(ge_transfer_pending, 0);
            SET_GPU32(list_id, rp_ge_readback_restart_list(c, GPU32(list_id)));
        }
        const uint32_t width = GPU16(transfer_size[0]), height = GPU16(transfer_size[1]);
        const uint32_t x = GPU16(transfer_origin[0]), y = GPU16(transfer_origin[1]);
        if (!width || width > 1024 || !height || height > 512)
            rp_block(c, "GPU_readback_rectangle_domain_not_reconstructed", 0x130FC);
        const uint32_t pixel_bytes = width * height * 2;
        const bool direct = !(destination & 15) && !(width & 7) &&
                            width + x <= 1024 && (height + y) * 2 <= 1024 &&
                            bytes >= pixel_bytes;
        selector = RP_GPU_READ_PIXELS;
        if (direct) {
            out = emit(c, out, UINT32_C(0xB5000000) | (((destination >> 24) & 15) << 16) | width);
            out = emit(c, out, UINT32_C(0xB4000000) | (destination & 0xFFFFFF));
            out = emit(c, out, UINT32_C(0xEB000000) | ((y & 511) << 10) | (x & 1023));
            out = emit(c, out, UINT32_C(0xEDFFFBFF) + width + (height << 10));
            out = emit(c, out, UINT32_C(0x13041B90));
            out = emit(c, out, UINT32_C(0x0A0000C4));
            rp_event(c, "host_adapter", "GPU_readback_cache_invalidation_coherent_host", destination, bytes);
            selector = RP_GPU_READ_FINISHED;
            next = (destination + pixel_bytes + 2) & ~UINT32_C(3);
        }
        SET_GPU8(read_selector, selector);
        SET_GPU32(list_cursor, out + 4);
        rp_event(c, "GPU_readback", direct ? "GE_copy_requires_barrier" : "CPU_read_requires_barrier",
                 destination, pixel_bytes);
        SET_GPU32(list_id, rp_ge_readback_barrier(c, GPU32(list_id), out + 4));
        SET_GPU8(ge_transfer_pending, 0);
        if (next == end) {
            SET_GPU32(transfer_read_latch, selector);
            return;
        }
        SET_GPU32(transfer_cursor, 0);
    }
    if (selector != RP_GPU_READ_PIXELS) {
        fill_tail(c, next, end, query_value(c, selector));
        return;
    }

    const uint32_t width = GPU16(transfer_size[0]), height = GPU16(transfer_size[1]);
    uint32_t column = GPU16(transfer_cursor[0]);
    uint32_t row = GPU16(transfer_cursor[1]);
    if (!width || width > 1024 || !height || height > 512 || column >= width || row >= height)
        rp_block(c, "GPU_readback_cursor_domain_not_reconstructed", 0x1316C);
    const uint32_t x = GPU16(transfer_origin[0]), y = GPU16(transfer_origin[1]);
    for (; next != end; next += 2) {
        const uint16_t pixel = rp_cd_u16(c, rp_gpu_vram_pixel(x + column, y + row));
        rp_cd_w16(c, next, pixel);
        if (++column == width) {
            column = 0;
            row = (row + 1) & 0xFFFF;
            SET_GPU16(transfer_cursor[1], row);
            if (row == height) {
                SET_GPU8(read_selector, RP_GPU_READ_FINISHED);
                /* The original aligns the address of the last halfword back
                 * to its word and retains any halfword not written above. */
                next &= ~UINT32_C(3);
                const uint32_t last = rp_u32(c, next);
                fill_tail(c, next + 4, end, last);
                return;
            }
        }
    }
    SET_GPU16(transfer_cursor[0], column);
    SET_GPU32(transfer_read_latch, RP_GPU_READ_PIXELS);
}

/* +0x12F90: tag clearing precedes the data helper. The DMA callback returns
 * the request size and does not schedule a GPU-ready event on this branch. */
uint32_t rp_pops_gpu_dma_readback(rp_context *c, uint32_t address, uint32_t bytes)
{
    const uint32_t ram = UINT32_C(0x09800000) | (address & 0x1FFFFF);
    if (!bytes || ((ram | bytes) & 3))
        rp_block(c, "GPU_DMA_readback_alignment_not_reconstructed", address);
    memset(rp_memory(c, ram + 0x400000, bytes), 0, bytes);
    rp_event(c, "GPU_readback", "DMA_code_tags_cleared", ram, bytes);
    rp_pops_gpu_read_data(c, ram, bytes);
    rp_event(c, "GPU_readback", "DMA_buffer_written", ram, bytes);
    return bytes;
}
