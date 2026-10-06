#include "pops_mdec.h"
#include "pops_gpu.h"
#include <string.h>

/* +0x1B678 installs the original DMA/I/O entries and reset constants.
 * Registering these callbacks is not yet decoding a macroblock. */
void rp_pops_mdec_reset(rp_context *c)
{
    rp_function(c, 0x1B678, "pops.mdec_reset_entries");
    rp_pops_install_dma(c, 0, 0xF54C);
    rp_pops_install_dma(c, 1, 0xF654);
    rp_pops_map_io(c, 0x1F801820, 8, 0xF6DC, 0xF70C);
    const size_t bytes = sizeof(((rp_core_mdec_layout *)0)->reset_parameters);
    memcpy(rp_memory(c, RP_MDEC_ADDRESS(c, reset_parameters), bytes),
           rp_module_memory(c, 0xD499C, bytes), bytes);
    rp_w32(c, RP_MDEC_ADDRESS(c, stream.event.callback), 0xE8F8);
}

/* +0xF6DC ignores access width and combines the original command flags,
 * signed pending-byte comparison and unsigned word count using ADDU. */
uint32_t rp_pops_mdec_read(rp_context *c, uint32_t address, uint32_t width)
{
    (void)width;
    rp_function(c, 0xF6DC, "pops.read_MDEC_status");
    const uint32_t command = rp_u32(c, RP_MDEC_ADDRESS(c, stream.command));
    const uint32_t remaining = rp_u32(c, RP_MDEC_ADDRESS(c, stream.remaining_bytes));
    uint32_t status = (0x100u + ((command >> 25) & 15)) << 23;
    status |= (uint32_t)((int32_t)remaining >= 5) << 29;
    status += remaining >> 2;
    rp_event(c, "mdec_register", "status_read", address, status);
    return status;
}

/* +0xF70C only latches the data command or resets the stream. It does not
 * consume compressed data; that work belongs to the two DMA callbacks. */
void rp_pops_mdec_write(rp_context *c, uint32_t address, uint32_t value)
{
    rp_function(c, 0xF70C, "pops.write_MDEC_port");
    if (!(address & 15)) {
        rp_w32(c, RP_MDEC_ADDRESS(c, stream.command), value);
        rp_event(c, "mdec_register", "command_latched", address, value);
        return;
    }
    if (!(value & UINT32_C(0x80000000))) return;
    if (rp_u32(c, RP_MDEC_ADDRESS(c, stream.event.prev)))
        rp_pops_remove_event(c, RP_MDEC_ADDRESS(c, stream.event));
    memset(rp_memory(c, RP_MDEC_ADDRESS(c, stream), sizeof(rp_mdec_stream_layout)),
           0, sizeof(rp_mdec_stream_layout));
    rp_pops_mdec_reset(c);
    rp_event(c, "milestone", "MDEC_control_reset_completed", address, value);
}

static float float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* +0xF54C: command 2 converts both 64-byte quantization banks using the
 * original halfword table and separate binary32 operations. Command 1
 * publishes compressed input; the decoder itself remains a separate body. */
uint32_t rp_pops_mdec_dma_input(rp_context *c, uint32_t address, uint32_t bytes, uint32_t control)
{
    (void)control;
    rp_function(c, 0xF54C, "pops.MDEC_DMA_input_partial");
    const uint32_t source = 0x09800000 | (address & 0x1FFFFF);
    const uint32_t command = rp_u32(c, RP_MDEC_ADDRESS(c, stream.command));
    const unsigned operation = command >> 29;
    if (operation == 1) {
        const uint32_t remaining = (command & 0xFFFF) * 4;
        rp_w32(c, RP_MDEC_ADDRESS(c, stream.remaining_bytes), remaining);
        rp_w32(c, RP_MDEC_ADDRESS(c, stream.input_cursor), source);
        rp_w32(c, RP_MDEC_ADDRESS(c, stream.input_bytes), bytes);
        rp_w8(c, RP_GPU_ADDRESS(c, display_mode_bytes[0]), 1);
        if (remaining && rp_u32(c, RP_MDEC_ADDRESS(c, stream.output_bytes)))
            rp_block(c, "MDEC_decoder_body_not_reconstructed", 0xE8F8);
        rp_event(c, "mdec_dma", "compressed_input_waiting_for_output", source, bytes);
        return 0;
    }
    if (operation != 2) return bytes;

    const uint8_t *coefficients = rp_memory(c, source, 128);
    const uint8_t *weights = rp_module_memory(c, 0xD49B8, 128);
    const float scale = float_from_bits(0x2A000000);
    const float first_bank_scale = float_from_bits(0x3FE2D0E5);
    for (unsigned i = 0; i < 128; ++i) {
        const unsigned index = i & 63, bank = i >> 6;
        const uint32_t weight = weights[index * 2] | (uint32_t)weights[index * 2 + 1] << 8;
        uint32_t product = coefficients[i] * weight;
        if (!index) product <<= 3;
        float value = (float)(int32_t)product * scale;
        if (!bank) value *= first_bank_scale;
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        rp_w32(c, RP_MDEC_ADDRESS(c, quantization_factors[index][bank]), bits);
    }
    rp_event(c, "milestone", "MDEC_quantization_factors_prepared", source, 128);
    return bytes;
}
