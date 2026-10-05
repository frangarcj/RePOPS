#include "runtime.h"
#include <string.h>

static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void put_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* +0x3764C. The no-existing-savedata-list path requires no file service.
 * Alternate directory scanning remains a separate host-filesystem boundary.
 * +0x6C8/+0x6CC are GP-relative, not base-zero module data addresses.
 */
void rp_pops_savedata_metadata(rp_context *c, uint32_t title, uint32_t id)
{
    rp_function(c, 0x3764C, "pops.savedata_metadata");
    uint8_t *counter = rp_memory(c, 0x10CAEF, 1);
    *counter = (uint8_t)(*counter + (*(uint8_t *)rp_memory(c, title, 1) == 0));
    const uint32_t override = rp_u32(c, c->gp + 0x6CC);
    if (override != UINT32_MAX) {
        memcpy(rp_memory(c, 0x471D98, 0x80), rp_memory(c, id, 0x80), 0x80);
        id = override;
    }
    if (*(uint8_t *)rp_memory(c, id, 1))
        memcpy(rp_memory(c, 0x451580, 0x80), rp_memory(c, id, 0x80), 0x80);
    memset(rp_memory(c, 0x450EBC, 0x28), 0xFF, 0x28);
    rp_w32(c, 0x450EB8, UINT32_MAX);
    const uint32_t list = rp_u32(c, c->gp + 0x6C8);
    rp_w32(c, 0x450EE4, list);
    if (list != UINT32_MAX && rp_u32(c, list) != 0)
        rp_block(c, "savedata_directory_scan_not_reconstructed", 0x3764C);
    if (*(uint8_t *)rp_memory(c, title, 1))
        memcpy(rp_memory(c, 0x451500, 0x80), rp_memory(c, title, 0x80), 0x80);
    rp_event(c, "milestone", "savedata_metadata_prepared", 0x451500, 0);
}

/* +0x24CA0 counts nonzero halfwords, not bytes. */
uint32_t rp_pops_halfword_length(rp_context *c, uint32_t text, int32_t limit)
{
    rp_function(c, 0x24CA0, "pops.halfword_length");
    uint32_t count = 0;
    while ((int32_t)count < limit && half(c, text + count * 2) != 0) ++count;
    return count;
}

/* +0x24CC8, in-header paths. External metadata payloads still need the
 * provider's E907AE69 service; do not treat them as an empty successful load.
 */
void rp_pops_optional_metadata(rp_context *c, uint32_t title, uint32_t id, uint32_t extra)
{
    rp_function(c, 0x24CC8, "pops.optional_header_metadata");
    const uint32_t buffer = 0x14D0F8;
    rp_w32(c, 0x14ED58, buffer);
    if (*(uint8_t *)rp_memory(c, extra, 1) == 0) {
        put_half(c, buffer, 0);
        rp_event(c, "milestone", "optional_metadata_absent", buffer, 0);
        return;
    }
    memcpy(rp_memory(c, buffer + 2, 0x3E), rp_memory(c, extra, 0x3E), 0x3E);
    if (half(c, buffer + 2) == 0xA9) {
        if (rp_u32(c, buffer + 4) != 0)
            rp_block(c, "external_metadata_provider_not_reconstructed", 0x24CC8);
        put_half(c, buffer, 0xA9);
        rp_w32(c, 0x14ED54, 0);
    } else {
        put_half(c, buffer, 0xA9);
        const uint32_t length = rp_pops_halfword_length(c, buffer + 2, 0x1F);
        rp_w32(c, 0x14ED54, length + 1);
    }
    memcpy(rp_memory(c, 0x10C450, 10), rp_memory(c, id, 10), 10);
    memcpy(rp_memory(c, 0x10C530, 0x80), rp_memory(c, title, 0x80), 0x80);
    rp_event(c, "milestone", "optional_metadata_prepared", buffer, rp_u32(c, 0x14ED54));
}

/* +0x1C590 absent-payload branch, used by the current single-disc input. */
uint32_t rp_pops_optional_auxiliary(rp_context *c, uint32_t present)
{
    rp_function(c, 0x1C590, "pops.optional_auxiliary_data");
    rp_w32(c, 0x14D07C, present);
    if (present) rp_block(c, "auxiliary_payload_decoder_not_reconstructed", 0x1C590);
    return 0;
}

void rp_pops_finish_disc_boot(rp_context *c)
{
    const uint32_t title = 0x09E8122C, id = 0x09E80400, extra = 0x09E812DC;
    rp_pops_savedata_metadata(c, title, id);
    rp_pops_optional_metadata(c, title, id, extra);
    (void)rp_pops_optional_auxiliary(c, rp_u32(c, 0x09E812B4) != 0);

    /* cdworker's first meaningful action (+0xDA80..+0xDA94) waits for event
     * bit 1 with mode 0x21. The newly created zero-bit event leaves it asleep.
     * This queues that initial wait only, not a reconstructed CD read worker.
     */
    const uint32_t event = ++c->next_id;
    rp_w32(c, c->gp + 0x3E3C, event);
    c->cd_event_bits = 0;
    c->cd_thread_entry = 0xDA3C;
    ++c->services;
    rp_event(c, "host_adapter", "create_cdread_event", event, 0);
    ++c->services;
    rp_event(c, "host_adapter", "queue_cdworker_waiting", c->cd_thread_entry, ++c->next_id);
    rp_event(c, "milestone", "disc_initialization_ready", 0x1B2F0, event);
}
