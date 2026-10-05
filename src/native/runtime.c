#include "runtime.h"
#include <stdlib.h>
#include <string.h>

void rp_event(rp_context *c, const char *kind, const char *name, uint32_t address, uint32_t value)
{
    /* Names are source-controlled identifiers, never arbitrary path contents. */
    fprintf(c->trace, "{\"kind\":\"%s\",\"name\":\"%s\",\"address\":%u,\"value\":%u}\n",
            kind, name, address, value);
    fflush(c->trace);
}

_Noreturn void rp_block(rp_context *c, const char *kind, uint32_t address)
{
    c->stop_kind = kind; c->stop_address = address;
    rp_event(c, "blocker", kind, address, 0);
    longjmp(c->stop, 1);
}

void *rp_memory(rp_context *c, uint32_t address, size_t length)
{
    /* GP=0x10000 addresses PSP's 16-KiB scratchpad, not module-relative code.
     * The analysis image also has offsets in this numerical range; code
     * introspection must explicitly use rp_module_memory instead.
     */
    if (address >= 0x10000 && address < 0x14000) {
        if (length > 0x14000 - address) rp_block(c,"scratchpad_access_overrun",address);
        return c->scratchpad + (address - 0x10000);
    }
    for (unsigned i = 0; i < 3; ++i) {
        rp_region *r = &c->regions[i];
        uint64_t offset = (uint64_t)address - r->base;
        if (address >= r->base && offset <= r->size && length <= r->size - offset)
            return r->bytes + offset;
    }
    rp_block(c, "unmapped_guest_memory", address);
}

void *rp_module_memory(rp_context *c, uint32_t offset, size_t length)
{
    if (offset > c->regions[0].size || length > c->regions[0].size - offset)
        rp_block(c,"module_relative_access_overrun",offset);
    return c->regions[0].bytes + offset;
}

uint32_t rp_module_u32(rp_context *c, uint32_t offset)
{
    const uint8_t *p=rp_module_memory(c,offset,4);
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

uint32_t rp_u32(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 4);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void rp_w32(rp_context *c, uint32_t address, uint32_t value)
{
    uint8_t *p = rp_memory(c, address, 4);
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
void rp_w8(rp_context *c, uint32_t address, uint8_t value)
{
    *(uint8_t *)rp_memory(c, address, 1) = value;
}

const char *rp_string(rp_context *c, uint32_t address)
{
    const char *s = rp_memory(c, address, 256);
    if (memchr(s, 0, 256) == NULL) rp_block(c, "unterminated_guest_string", address);
    return s;
}

uint32_t rp_alloc(rp_context *c, uint32_t size)
{
    uint32_t aligned = (size + 15u) & ~15u;
    if (size > 0x400000 || aligned > 0x1800000 - c->heap_next)
        rp_block(c, "host_guest_heap_limit", c->heap_next);
    uint32_t result = c->heap_next;
    c->heap_next += aligned ? aligned : 16;
    rp_memory(c, result, size);
    return result;
}

void rp_function(rp_context *c, uint32_t address, const char *name)
{
    ++c->functions; c->last_function = address;
    rp_event(c, "native_c_function", name, address, c->functions);
}

/* Native adapter for the observed open/PSAR-offset service. The path/context
 * are supplied by the harness, not by a reconstructed POPSMAN startup yet.
 * UID-kernel-object flags are not reproduced by this filesystem adapter.
 */
int32_t rp_provider_open_image(rp_context *c, uint32_t psar_output)
{
    ++c->services;
    rp_event(c, "host_adapter", "sceMeAudio_8D5A07D2_open_image", 0x8D5A07D2, psar_output);
    if (!c->disc_path) rp_block(c, "game_pbp_not_supplied", 0x1B56C);
    c->disc = fopen(c->disc_path, "rb");
    if (!c->disc) rp_block(c, "game_pbp_open_failed", 0x1B56C);
    uint8_t header[40];
    if (fread(header, 1, sizeof(header), c->disc) != sizeof(header) ||
        memcmp(header, "\0PBP", 4) != 0)
        rp_block(c, "input_is_not_a_pbp", 0x1B56C);
    if (fseek(c->disc, 0, SEEK_END) != 0)
        rp_block(c, "pbp_size_query_failed", 0x1B56C);
    const long file_size = ftell(c->disc);
    if (file_size < 40) rp_block(c, "truncated_pbp", 0x1B56C);
    c->disc_bytes = (uint64_t)file_size;
    uint32_t previous = 40;
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *p = header + 8 + i * 4;
        const uint32_t offset = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                                (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        if (offset < previous || offset > c->disc_bytes)
            rp_block(c, "invalid_pbp_component_offsets", 0x1B56C);
        previous = offset;
    }
    c->psar_offset = (uint32_t)header[36] | (uint32_t)header[37] << 8 |
                     (uint32_t)header[38] << 16 | (uint32_t)header[39] << 24;
    const uint32_t psp_offset = (uint32_t)header[32] | (uint32_t)header[33] << 8 |
                                (uint32_t)header[34] << 16 | (uint32_t)header[35] << 24;
    uint8_t tag[4];
    if (c->psar_offset - psp_offset < 4 ||
        fseek(c->disc, (long)psp_offset, SEEK_SET) != 0 || fread(tag, 1, 4, c->disc) != 4)
        rp_block(c, "truncated_data_psp_tag", 0x1B56C);
    c->data_psp_word = (uint32_t)tag[0] | (uint32_t)tag[1] << 8 |
                       (uint32_t)tag[2] << 16 | (uint32_t)tag[3] << 24;
    c->disc_header.valid = 0;
    rp_w32(c, psar_output, c->psar_offset);
    rewind(c->disc);
    rp_event(c, "milestone", "opened_local_pbp", 0x1B56C, c->psar_offset);
    return 1;
}

/* POPSMAN +0x23D0 boundary: exact seek followed by a bounded read. */
int32_t rp_provider_read_at(rp_context *c, uint32_t dest, uint32_t offset, uint32_t size)
{
    ++c->services;
    rp_event(c, "host_adapter", "sceMeAudio_30BE34E4_read_at", offset, size);
    if (!c->disc || fseek(c->disc, (long)offset, SEEK_SET) != 0) return -1;
    return (int32_t)fread(rp_memory(c, dest, size), 1, size, c->disc);
}
