#include "../src/native/pops_mdec.h"
#include "../src/native/pops_gpu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned dma_installs, io_installs, removals;
void rp_pops_install_dma(rp_context *c, uint32_t channel, uint32_t handler)
{
    (void)c;
    assert(channel == dma_installs % 2);
    assert(handler == (channel ? 0xF654 : 0xF54C));
    ++dma_installs;
}
void rp_pops_map_io(rp_context *c, uint32_t address, uint32_t bytes,
                    uint32_t reader, uint32_t writer)
{
    (void)c;
    assert(address == 0x1F801820 && bytes == 8);
    assert(reader == 0xF6DC && writer == 0xF70C);
    ++io_installs;
}
void rp_pops_remove_event(rp_context *c, uint32_t event)
{
    assert(event == RP_MDEC_ADDRESS(c, stream.event));
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.command)) == 0xA5A5A5A5);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.event.prev)));
    ++removals; /* Real scheduler unlink/time accounting is tested separately. */
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0, 0x100000, calloc(1, 0x100000)};
    c->regions[1] = (rp_region){0x09800000, 0x1000, calloc(1, 0x1000)};
    assert(c->regions[0].bytes && c->regions[1].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    uint8_t expected[28];
    for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)(0x40 + i);
    memcpy(rp_module_memory(c, 0xD499C, sizeof(expected)), expected, sizeof(expected));
    rp_pops_mdec_reset(c);
    assert(dma_installs == 2 && io_installs == 1);
    assert(!memcmp(rp_memory(c, RP_MDEC_ADDRESS(c, reset_parameters), 28), expected, 28));
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.event.callback)) == 0xE8F8);

    assert(rp_pops_mdec_read(c, 0x1F801824, 2) == 0x80000000);
    rp_pops_mdec_write(c, 0x1F801820, 0x1E000000);
    const uint32_t counts[] = {4, 5, 8, UINT32_MAX};
    const uint32_t statuses[] = {0x87800001, 0xA7800001, 0xA7800002, 0xC77FFFFF};
    for (unsigned i = 0; i < 4; ++i) {
        rp_w32(c, RP_MDEC_ADDRESS(c, stream.remaining_bytes), counts[i]);
        assert(rp_pops_mdec_read(c, 0x1F801824, i) == statuses[i]);
    }
    rp_pops_mdec_write(c, 0x1F801824, 0x60000000);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.command)) == 0x1E000000);
    assert(io_installs == 1);

    const uint32_t stream = RP_MDEC_ADDRESS(c, stream);
    rp_w32(c, stream - 4, 0x12345678);
    rp_w32(c, stream + sizeof(rp_mdec_stream_layout), 0x87654321);
    memset(rp_memory(c, stream, sizeof(rp_mdec_stream_layout)), 0xA5, sizeof(rp_mdec_stream_layout));
    rp_pops_mdec_write(c, 0x1F801824, 0x80000000);
    assert(removals == 1 && dma_installs == 4 && io_installs == 2);
    for (unsigned offset = 0; offset < sizeof(rp_mdec_stream_layout); offset += 4)
        assert(rp_u32(c, stream + offset) == (offset == 40 ? 0xE8F8 : 0));
    assert(rp_u32(c, stream - 4) == 0x12345678);
    assert(rp_u32(c, stream + sizeof(rp_mdec_stream_layout)) == 0x87654321);
    assert(!memcmp(rp_memory(c, RP_MDEC_ADDRESS(c, reset_parameters), 28), expected, 28));

    uint8_t *weights = rp_module_memory(c, 0xD49B8, 128);
    for (unsigned i = 0; i < 64; ++i) { weights[i * 2] = 1; weights[i * 2 + 1] = 0; }
    weights[126] = 0xFF; weights[127] = 0xFF;
    memset(rp_memory(c, 0x09800100, 64), 1, 64);
    memset(rp_memory(c, 0x09800140, 64), 3, 64);
    rp_w8(c, 0x0980013F, 255); rp_w8(c, 0x0980017F, 255);
    rp_pops_mdec_write(c, 0x1F801820, 0x40000001);
    assert(rp_pops_mdec_dma_input(c, 0x100, 128, 0x01000201) == 128);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[0][0])) == 0x2BE2D0E5);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[0][1])) == 0x2C400000);
    for (unsigned i = 1; i < 63; ++i) {
        assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[i][0])) == 0x2A62D0E5);
        assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[i][1])) == 0x2AC00000);
    }
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[63][0])) == 0x3661ED32);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, quantization_factors[63][1])) == 0x35FEFF01);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.command)) == 0x40000001);

    rp_pops_mdec_write(c, 0x1F801820, 0x60000000);
    assert(rp_pops_mdec_dma_input(c, 0x100, 64, 0) == 64);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0x12345678);
    rp_pops_mdec_write(c, 0x1F801820, 0x20000012);
    assert(rp_pops_mdec_dma_input(c, 0x80000100, 64, 0) == 0);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.remaining_bytes)) == 72);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.input_bytes)) == 64);
    assert(rp_u32(c, RP_MDEC_ADDRESS(c, stream.input_cursor)) == 0x09800100);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, display_mode)) == 0x12345601);
    rp_w32(c, RP_MDEC_ADDRESS(c, stream.output_bytes), 128);
    if (!setjmp(c->stop)) {
        (void)rp_pops_mdec_dma_input(c, 0x100, 64, 0);
        assert(!"Compressed macroblock was falsely decoded");
    }
    assert(!strcmp(c->stop_kind, "MDEC_decoder_body_not_reconstructed"));
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c);
    puts("MDEC: ports, reset ordering, quantization banks and compressed-input handoff passed.");
    return 0;
}
