#include "../src/native/pops_gpu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* This fixture supplies completed GE output, not a renderer. Its hand-written
 * command expectation and nonzero pixels test the recovered CPU/GE boundary. */
static bool allow_barrier, direct_fixture;
static unsigned barriers, restarts;
static uint32_t expected_old_list;
static const uint32_t buffer = 0x09800100, commands = 0x49A00000;

uint32_t rp_ge_readback_restart_list(rp_context *c, uint32_t old_list)
{
    assert(old_list == expected_old_list);
    assert(!rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)));
    ++restarts;
    return ++expected_old_list;
}

uint32_t rp_ge_readback_barrier(rp_context *c, uint32_t old_list, uint32_t continuation)
{
    ++barriers;
    assert(old_list == expected_old_list);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)) == continuation);
    if (!allow_barrier) rp_block(c, "GE_readback_execution_required", 0x13148);
    if (direct_fixture) {
        const uint32_t expected[] = {0xB5090008, 0xB4800100, 0xEB005010,
                                     0xEE000407, 0x13041B90, 0x0A0000C4};
        assert(continuation == commands + sizeof(expected) + 4);
        assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == RP_GPU_READ_FINISHED);
        for (unsigned i = 0; i < 6; ++i) assert(rp_u32(c, commands + i * 4) == expected[i]);
        for (unsigned i = 0; i < 16; ++i) {
            assert(rp_cd_u16(c, buffer + i * 2) == 0xA5A5);
            rp_cd_w16(c, buffer + i * 2, (uint16_t)(0x1230 + i));
        }
    } else {
        assert(continuation == commands + 4);
        assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == RP_GPU_READ_PIXELS);
    }
    return ++expected_old_list;
}

static void reset(rp_context *c, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    memset(rp_memory(c, buffer - 4, 96), 0xA5, 96);
    memset(rp_memory(c, commands, 128), 0xCC, 128);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), commands);
    rp_w32(c, RP_GPU_ADDRESS(c, transfer_origin), x | (y << 16));
    rp_w32(c, RP_GPU_ADDRESS(c, transfer_size), width | (height << 16));
    rp_w32(c, RP_GPU_ADDRESS(c, transfer_cursor), 0xABCD0123);
    rp_w8(c, RP_GPU_ADDRESS(c, read_selector), RP_GPU_READ_START);
    rp_w32(c, RP_GPU_ADDRESS(c, list_id), 77);
    expected_old_list = 77;
    barriers = restarts = 0;
    direct_fixture = false;
    allow_barrier = true;
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0x09800000, 0x600000, calloc(1, 0x600000)};
    c->regions[1] = (rp_region){0x04000000, 0x100000, calloc(1, 0x100000)};
    assert(c->regions[0].bytes && c->regions[1].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "Unexpected stop: %s\n", c->stop_kind); return 1; }

    /* Unaligned-for-GE destination, both coordinate axes wrap, and the CPU
     * transfer survives multiple requests. Values are not synthesized zeros. */
    reset(c, 1023, 511, 3, 2);
    const uint32_t xy[][2] = {{1023,511},{0,511},{1,511},{1023,0},{0,0},{1,0}};
    for (unsigned i = 0; i < 6; ++i)
        rp_cd_w16(c, rp_gpu_vram_pixel(xy[i][0], xy[i][1]), (uint16_t)(0x1101 + i));
    rp_pops_gpu_read_data(c, buffer + 4, 4);
    assert(barriers == 1 && !restarts);
    assert(rp_u32(c, buffer + 4) == 0x11021101);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_cursor)) == 2);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == RP_GPU_READ_PIXELS);
    rp_pops_gpu_read_data(c, buffer + 8, 8);
    assert(barriers == 1);
    assert(rp_u32(c, buffer + 8) == 0x11041103);
    assert(rp_u32(c, buffer + 12) == 0x11061105);
    assert(rp_cd_u8(c, RP_GPU_ADDRESS(c, read_selector)) == RP_GPU_READ_FINISHED);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == 0x11061105);
    /* A subsequent call with selector FF returns FF, not the previous latch. */
    rp_pops_gpu_read_data(c, buffer + 16, 8);
    assert(rp_u32(c, buffer + 16) == 255 && rp_u32(c, buffer + 20) == 255);

    reset(c, 1023, 511, 3, 1);
    rp_pops_gpu_read_data(c, buffer + 4, 16);
    assert(rp_u32(c, buffer + 4) == 0x11021101);
    assert(rp_u32(c, buffer + 8) == 0xA5A51103);
    assert(rp_u32(c, buffer + 12) == 0xA5A51103);
    assert(rp_u32(c, buffer + 16) == 0xA5A51103);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == 0xA5A51103);

    reset(c, 16, 20, 8, 2);
    direct_fixture = true;
    rp_w8(c, RP_GPU_ADDRESS(c, ge_transfer_pending), 0xFF);
    rp_pops_gpu_read_data(c, buffer, 32);
    assert(restarts == 1 && barriers == 1);
    for (unsigned i = 0; i < 16; ++i) assert(rp_cd_u16(c, buffer + i * 2) == 0x1230 + i);
    assert(rp_u32(c, buffer + 32) == 0xA5A5A5A5);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_read_latch)) == 255);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, transfer_cursor)) == 0xABCD0123);

    reset(c, 16, 20, 8, 2);
    direct_fixture = true;
    memset(rp_memory(c, buffer + 0x400000 - 4, 48), 0xAB, 48);
    assert(rp_pops_gpu_dma_readback(c, 0x100, 40) == 40);
    for (unsigned i = 0; i < 40; ++i) assert(!rp_cd_u8(c, buffer + 0x400000 + i));
    assert(rp_u32(c, buffer + 0x400000 - 4) == 0xABABABAB);
    assert(rp_u32(c, buffer + 0x400000 + 40) == 0xABABABAB);
    assert(rp_u32(c, buffer + 32) == 255 && rp_u32(c, buffer + 36) == 255);
    assert(!rp_u32(c, RP_GPU_ADDRESS(c, transfer_cursor)));

    /* Query DMA uses the same buffer helper and does not call a GE barrier. */
    reset(c, 0, 0, 1, 1);
    rp_w8(c, RP_GPU_ADDRESS(c, read_selector), 2);
    rp_w32(c, RP_GPU_ADDRESS(c, texture_window), 0x31415);
    assert(rp_pops_gpu_dma_readback(c, 0x100, 16) == 16);
    for (unsigned i = 0; i < 4; ++i) assert(rp_u32(c, buffer + i * 4) == 0x31415);
    assert(!barriers);

    /* The native dependency must stop before consuming the allocated shadow
     * when no GE output has been supplied. Code-tag writes may precede it. */
    reset(c, 16, 20, 8, 2);
    allow_barrier = false;
    if (!setjmp(c->stop)) {
        (void)rp_pops_gpu_dma_readback(c, 0x100, 32);
        assert(!"Readback returned without a completed backend");
    }
    assert(!strcmp(c->stop_kind, "GE_readback_execution_required"));
    assert(barriers == 1 && rp_u32(c, buffer) == 0xA5A5A5A5);
    assert(rp_u32(c, commands) == 0xB5090008);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("GPU readback: direct GE words, nonzero CPU pixels, wrap, partial/odd tails, tags and barrier refusal passed; no renderer.");
    return 0;
}
