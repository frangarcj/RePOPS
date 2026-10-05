#include "runtime.h"
#include <string.h>

/* +0x1B6EC. All state references are GP-relative, despite misleading globals
 * in the base-zero Ghidra output. Arithmetic is explicitly modulo 2^32.
 */
void rp_pops_config_postprocess(rp_context *c)
{
    rp_function(c, 0x1B6EC, "pops.configuration_postprocess");
    const uint32_t value = rp_u32(c, c->gp + 0x6E8);
    if ((int32_t)value < 0x10000) {
        const uint32_t next = value + 1;
        const uint32_t encoded = (int32_t)next < 0 ? 0x10000 - next : (next << 8) + 0x10000;
        rp_w32(c, c->gp + 0x6E8, encoded);
    }
    const uint32_t mode = rp_u32(c, c->gp + 0x6D0);
    if (mode <= 1) {
        static const uint32_t words[2][4] = {
            {0xE2000F01, 0xE3000100, 0xE400000F, 0xE500F001},
            {0xE200D9C8, 0xE300BFAE, 0xE400C8D9, 0xE500AEBF}
        };
        for (unsigned i = 0; i < 4; ++i) rp_w32(c, 0x041B9304 + i * 4, words[mode][i]);
    }
}

/* +0x24770, supported protocol domain: null ID or normalized AAAA12345 ID.
 * Host bounds reject malformed static-table references instead of treating
 * arbitrary guest integers as native pointers. No compatibility row is invented.
 */
uint32_t rp_pops_apply_game_config(rp_context *c, uint32_t id_address,
                                  uint32_t version, uint32_t required, uint32_t extra)
{
    rp_function(c, 0x24770, "pops.per_game_configuration");
    if (required > UINT32_C(0x06060000)) return UINT32_C(0x80000002);
    memset(rp_memory(c, c->gp + 0x6B0, 0x80), 0xFF, 0x80);
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t offsets[] = {0x6AC, 0x6F0, 0x70C, 0x710};
        rp_w32(c, c->gp + offsets[i], 0);
    }
    if (!id_address) return 0;

    const uint8_t *id = rp_memory(c, id_address, 10);
    uint32_t prefix = 0, suffix = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (id[i] < 'A' || id[i] > 'Z') rp_block(c, "config_id_domain_not_supported", id_address);
        prefix = (prefix << 8) | id[i];
    }
    for (unsigned i = 4; i < 9; ++i) {
        if (id[i] < '0' || id[i] > '9') rp_block(c, "config_id_domain_not_supported", id_address);
        suffix = (suffix << 4) | (id[i] - '0');
    }
    if (id[9] != 0) rp_block(c, "config_id_domain_not_supported", id_address);
    if (id[2] == 'P') {
        memcpy(rp_module_memory(c, 0x6BC20, 0x16120),
               rp_module_memory(c, 0x3DB00, 0x16120), 0x16120);
    }
    const uint32_t key = prefix ^ (suffix * UINT32_C(0x1001));
    unsigned visited = 0;
    for (uint32_t row = 0xEF0B0;; row += 12) {
        if (++visited > 4096) rp_block(c, "unterminated_game_config_table", row);
        const uint32_t candidate = rp_module_u32(c, row);
        if (candidate == 0) break;
        if (candidate != key) continue;
        const uint32_t count = rp_module_u32(c, row + 4);
        const uint32_t data = rp_module_u32(c, row + 8);
        if ((int32_t)count > 4096) rp_block(c, "game_config_row_limit", row);
        for (uint32_t i = 0; (int32_t)i < (int32_t)count; ++i) {
            if ((uint64_t)data + i * 8 + 8 > c->regions[0].size)
                rp_block(c, "game_config_data_overrun", data);
            const uint32_t index = rp_module_u32(c, data + i * 8);
            const uint32_t entry = rp_module_u32(c, data + i * 8 + 4);
            if ((int32_t)index < 0) rp_w32(c, c->gp + 0x6AC, entry);
            else {
                if (index >= 32) rp_block(c, "game_config_index_not_supported", index);
                rp_w32(c, c->gp + 0x6B0 + index * 4, entry);
            }
        }
        rp_event(c, "milestone", "game_config_table_match", row, count);
        break;
    }
    if (memcmp(id, rp_module_memory(c, 0xF18E8, 4), 4) == 0) {
        rp_w32(c, c->gp + 0x6C0, UINT32_MAX - 1);
        rp_w32(c, c->gp + 0x6B4, 0xE8);
        rp_w32(c, c->gp + 0x6B8, 12);
        rp_w32(c, c->gp + 0x6DC, 0x10004);
        rp_w32(c, c->gp + 0x6BC, 12);
    }
    if (version > UINT32_C(0x06060000)) {
        memcpy(rp_memory(c, c->gp + 0x6AC, 4), rp_memory(c, extra, 4), 4);
        if (extra > UINT32_MAX - 4) rp_block(c, "game_config_extra_overflow", extra);
        memcpy(rp_memory(c, c->gp + 0x6B0, 0x80), rp_memory(c, extra + 4, 0x80), 0x80);
    }
    rp_pops_config_postprocess(c);
    rp_w32(c, c->gp + 0x6C4, rp_u32(c, c->gp + 0x6C4) + 1);
    const uint32_t flags = rp_u32(c, c->gp + 0x6B0);
    if (flags == UINT32_MAX || !(flags & 4)) rp_w32(c, c->gp + 0x6D8, 2);
    else if ((int32_t)rp_u32(c, c->gp + 0x6D8) < 0) rp_w32(c, c->gp + 0x6D8, 0xFFFF);
    rp_event(c, "milestone", "per_game_configuration_applied", 0x24770, key);
    return 0;
}

/* +0xADE8: preserve the original nibble arithmetic even for non-BCD bytes. */
int32_t rp_pops_msf_to_sector(rp_context *c, uint32_t address)
{
    rp_function(c, 0xADE8, "pops.msf_to_sector");
    const uint8_t *p = rp_memory(c, address, 3);
    const int m = p[0] - 6 * (p[0] >> 4);
    const int s = p[1] - 6 * (p[1] >> 4);
    const int f = p[2] - 6 * (p[2] >> 4);
    return (m * 60 + s) * 75 + f - 150;
}
