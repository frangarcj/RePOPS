#include "runtime.h"
#include <string.h>

#define DISC_HEADER_BYTES UINT32_C(0xB3C80)
#define DISC_EXTRA_BYTES UINT32_C(0xB3880)

/* Admission check for our unprotected-format adapter, NOT authentication.
 * Protected/unknown formats are not routed through a fake BBMac success.
 */
static int plain_id_supported(const uint8_t *id)
{
    unsigned output = 0;
    for (unsigned input = 0; input < 32; ++input) {
        const uint8_t ch = id[input];
        if (ch == 0) return output == 9;
        if (ch == '_' || ch == '-') continue;
        if (output >= 9 || (output < 4 ? ch < 'A' || ch > 'Z' : ch < '0' || ch > '9'))
            return 0;
        ++output;
    }
    return 0;
}

/* POPSMAN +0x050C single-disc field updates, using an explicit plain-file
 * adapter for +0x0D3C. The native harness does not implement the protected-file
 * IOCTLs, BBMac branch, K1 privileges or BC100040 hardware read.
 */
uint32_t rp_provider_plain_disc_header(rp_context *c, uint32_t header, uint32_t disc)
{
    ++c->services;
    c->disc_header.valid = 0;
    rp_event(c, "format_adapter", "plain_PSISOIMG_extended_header", header, disc);
    if (disc != 0)
        rp_block(c, "multidisc_provider_not_reconstructed", 0x14447BA0);
    if (c->data_psp_word != UINT32_C(0x464C457F))
        rp_block(c, "protected_or_unknown_pbp_provider", 0x14447BA0);
    if (header > UINT32_MAX - DISC_HEADER_BYTES)
        rp_block(c, "disc_header_address_overflow", header);
    uint8_t *data = rp_memory(c, header, DISC_HEADER_BYTES);
    if (memcmp(data, "PSISOIMG0000", 12) != 0) return UINT32_MAX;
    const uint64_t offset = (uint64_t)c->psar_offset + 0x400;
    if (offset > UINT32_MAX || offset + DISC_EXTRA_BYTES > c->disc_bytes)
        return UINT32_MAX;
    if (rp_provider_read_at(c, header + 0x400, (uint32_t)offset, DISC_EXTRA_BYTES) != (int32_t)DISC_EXTRA_BYTES)
        return UINT32_MAX;
    if (!plain_id_supported(data + 0x400))
        rp_block(c, "protected_or_unknown_disc_header", 0x14447BA0);

    c->disc_header.auxiliary_offset = c->psar_offset + rp_u32(c, header + 0x12B4);
    c->disc_header.auxiliary_size = rp_u32(c, header + 0x12B8);
    c->disc_header.offset_1220 = ((uint64_t)rp_u32(c, header + 0x1224) << 32) |
                               rp_u32(c, header + 0x1220);
    c->disc_header.offset_1220 += c->psar_offset;
    c->disc_header.valid = 1;
    rp_event(c, "deferred_hardware", "provider_BC100040_mode_not_sampled", 0xBC100040, 0);
    const uint32_t result = c->data_psp_word ^ UINT32_C(0x4A08B53F);
    rp_event(c, "milestone", "extended_disc_header_loaded", header, DISC_HEADER_BYTES);
    rp_event(c, "provider_result", "result_derived_from_DATA_PSP_word", 0x14447BA0, result);
    return result;
}

/* POPS +0x28730: store the first provider result, preserving the return word. */
uint32_t rp_pops_remember_provider_result(rp_context *c, uint32_t result)
{
    rp_function(c, 0x28730, "pops.remember_provider_result");
    if (rp_u32(c, 0x14D8F4) != 1) {
        rp_w32(c, 0x14D8F4, 1);
        rp_w32(c, 0x49CBE0, result ^ UINT32_C(0x9136B780));
    }
    return result;
}

/* POPS +0x1B61C: V0 remains the original pointer, despite Ghidra's void type.
 * The supported input is a 32-byte header field. A host guard refuses an
 * unterminated separator run instead of reading into the next header field.
 */
uint32_t rp_pops_normalize_disc_id(rp_context *c, uint32_t address, int32_t limit)
{
    rp_function(c, 0x1B61C, "pops.normalize_disc_id");
    if (limit <= 0) return address;
    if (limit > 32) rp_block(c, "disc_id_normalizer_limit", address);
    uint8_t *id = rp_memory(c, address, 32);
    unsigned output = 0;
    for (unsigned input = 0; input < 32; ++input) {
        const uint8_t ch = id[input];
        if (ch != '_' && ch != '-') {
            id[output++] = ch;
            if (ch == 0 || output == (uint32_t)limit) return address;
        }
    }
    rp_block(c, "unterminated_disc_id", address);
}

/* POPS +0x1AF90 checks four uppercase letters followed by five digits. */
uint32_t rp_pops_check_disc_id(rp_context *c, uint32_t address)
{
    rp_function(c, 0x1AF90, "pops.check_disc_id");
    const uint8_t *id = rp_memory(c, address, 9);
    for (unsigned i = 0; i < 9; ++i) {
        if (i < 4 ? id[i] < 'A' || id[i] > 'Z' : id[i] < '0' || id[i] > '9')
            return UINT32_MAX;
    }
    return 0;
}

/* Remaining +0x1B004 path after configuration. Optional auxiliary/extra
 * entries remain explicit blockers. The index offsets become absolute file
 * offsets; no game sector is executed or decompressed by this function.
 */
uint32_t rp_pops_finalize_disc_selection(rp_context *c, uint32_t selected, uint32_t disc_offset)
{
    const uint32_t header = UINT32_C(0x09E80000);
    rp_w8(c, c->gp + 0x3E45, (uint8_t)selected);
    const uint32_t sectors = (uint32_t)rp_pops_msf_to_sector(c, header + 0x81B);
    rp_w32(c, c->gp + 0x730, sectors - 1);
    if (rp_u32(c, 0x49CBD0) != 0)
        rp_block(c, "previous_disc_auxiliary_release_not_reconstructed", 0x1B004);
    if (rp_u32(c, header + 0x12D4) != 0)
        rp_block(c, "optional_disc_auxiliary_table_not_reconstructed", 0x1B004);
    const uint64_t base64 = (uint64_t)disc_offset + rp_u32(c, header + 0xBFC);
    if (base64 > UINT32_MAX) rp_block(c, "disc_data_offset_overflow", 0x1B004);
    const uint32_t base = (uint32_t)base64;
    uint32_t last = rp_u32(c, c->gp + 0x730) - 1;
    const uint8_t *h = rp_memory(c, header, DISC_HEADER_BYTES);
    const int first_track = h[0x807] - 6 * (h[0x807] >> 4);
    const int last_track = h[0x811] - 6 * (h[0x811] >> 4);
    if (!(rp_u32(c, c->gp + 0x6AC) & 0x2000) && last_track - first_track + 1 > 1)
        last = (uint32_t)rp_pops_msf_to_sector(c, header + 0x82F);
    const uint32_t count = (last >> 4) + 1;
    if (count > (DISC_HEADER_BYTES - 0x4000) / 32)
        rp_block(c, "disc_block_index_out_of_header", 0x1B004);
    for (uint32_t remaining = count; remaining > 0; --remaining) {
        const uint32_t row = header + 0x4000 + (remaining - 1) * 32;
        const uint8_t *p = rp_memory(c, row + 4, 2);
        const uint32_t length = p[0] | (uint32_t)p[1] << 8;
        const uint64_t offset64 = (uint64_t)rp_u32(c, row) + base;
        if (offset64 > UINT32_MAX) rp_block(c, "disc_block_offset_overflow", row);
        const uint32_t offset = (uint32_t)offset64;
        const uint32_t read_size = length == 0x9300 ? UINT32_MAX :
                                   length == 0 ? 0 : (length + (offset & 0x1FF) + 0x1FF) & ~UINT32_C(0x1FF);
        rp_w32(c, row, offset);
        rp_w32(c, row + 0x18, read_size);
    }
    if (rp_u32(c, header + 0xC04) != 0)
        rp_block(c, "additional_disc_entries_not_reconstructed", 0x1B004);
    rp_event(c, "milestone", "disc_block_table_prepared", header + 0x4000, count);
    return 0;
}
