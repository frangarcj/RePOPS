#include "pops_emit.h"
#include <string.h>

#define RECORD_BASE UINT32_C(0x041B0000)

static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t emit(rp_context *c, uint32_t out, uint32_t word)
{
    rp_w32(c, out, word); return out + 4;
}

/* +0x4B44: source-code validation, not a host-side replacement checksum.
 * Emit the original check so the existing execution engine observes changes.
 */
uint32_t rp_emit_ram_guard(rp_context *c, uint32_t pc, uint32_t out, uint32_t words)
{
    rp_function(c, 0x4B44, "pops.emit_RAM_code_guard");
    uint32_t count = half(c, c->gp + 0xB40) & 0x4000 ? 0xFFFF : rp_u32(c, c->gp + 0x6D8);
    if ((int32_t)words < (int32_t)count) count = words;
    const uint32_t source = (rp_u32(c, c->gp + 0xB50) & 0x1FFFFF) | 0x09800000;
    if ((int32_t)count < 3) {
        uint32_t selected = pc;
        if (words > 2 && (rp_u32(c, source) & 0xFFFF8000) == 0x27BD8000) {
            selected += 4;
            if (words > 3 && (rp_u32(c, source + 4) & 0xFFFF8000) == 0xAFBF0000) selected += 4;
        }
        const uint32_t first = (selected & 0x1FFFFF) | 0x09800000, next = first + 4;
        const uint32_t upper = (first >> 16) + !!(first & 0x8000);
        const uint32_t next_upper = (next >> 16) + !!(next & 0x8000);
        const uint32_t sum = rp_u32(c, first) + rp_u32(c, next);
        uint32_t base = 29;
        if (upper != 0x980) {
            out = emit(c, out, 0x3C020000 | upper);
            base = 2;
            rp_w32(c, c->gp + 0x744, out);
            rp_w32(c, c->gp + 0x74C, upper);
        }
        if (upper == next_upper) out = emit(c, out, 0x8C030000 | (base << 21) | (next & 0xFFFF));
        else {
            out = emit(c, out, 0x3C030000 | next_upper);
            out = emit(c, out, 0x8C630000 | (next & 0xFFFF));
        }
        out = emit(c, out, 0x8C040000 | (base << 21) | (selected & 0xFFFF));
        if (sum) out = rp_emit_constant(c, out, 5, sum);
        out = emit(c, out, 0x00832021);
        const uint32_t branch = (sum ? 0x14850000 : 0x14800000) |
            (((rp_u32(c, c->gp + 0x3CF0) - out - 4) >> 2) & 0xFFFF);
        out = emit(c, out, branch);
        out = emit(c, out, 0);
        rp_w32(c, c->gp + 0x740, out);
        return out;
    }

    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) sum += rp_u32(c, source + i * 4);
    const uint32_t tail = count & 3;
    out = rp_emit_constant(c, out, 4, source + tail * 4);
    for (uint32_t i = 1; i <= tail; ++i)
        out = emit(c, out, 0x8C800000 | (i << 16) | ((0u - i * 4) & 0xFFFF));
    if (count != tail) out = emit(c, out, 0x24850000 | (((count - tail) * 4) & 0xFFFF));
    out = rp_emit_constant(c, out, 6, sum);
    for (uint32_t i = 1; i <= tail; ++i) out = emit(c, out, 0x00C03023 | (i << 16));
    if (count == tail) {
        out = emit(c, out, 0x14C00000 | (((rp_u32(c, c->gp + 0x3CF0) - out - 4) >> 2) & 0xFFFF));
        return emit(c, out, 0);
    }
    return rp_emit_jump_delay(c, out, 0x30002918);
}

static uint32_t nearby_invalidation_stub(rp_context *c, uint32_t out)
{
    if (!(half(c, c->gp + 0xB40) & 0x8000) &&
            out > rp_u32(c, c->gp + 0x3CF0) + 0x1FFD0) {
        rp_w32(c, c->gp + 0x3CF0, out);
        out = emit(c, out, 0x0C000A1E);
        out = emit(c, out, 0);
    }
    return out;
}

static uint32_t publish_ram(rp_context *c, uint32_t pc, uint32_t entry,
                            uint32_t length_slot, uint32_t out)
{
    const uint32_t mode = half(c, c->gp + 0xB40);
    const uint32_t end = rp_u32(c, c->gp + 0xB4C) + 16;
    if (!c->vfpu_zero_ready) rp_block(c, "RAM_compiler_vector_fill_not_initialized", 0x665C);
    for (uint32_t record = RECORD_BASE; record < end; record += 16) {
        const uint32_t flags = half(c, record);
        if (flags & 2) {
            const uint32_t patch = rp_u32(c, record + 12);
            const uint32_t target_record = RECORD_BASE + (rp_u32(c, record + 8) - pc) * 4;
            const uint32_t target = rp_u32(c, target_record + 4), word = rp_u32(c, patch);
            if (word >> 27 == 1)
                rp_w32(c, patch, target == patch ? 0 : (word & 0xFC000000) | ((target >> 2) & 0x3FFFFFF));
            else rp_w32(c, patch, (word & 0xFFFF0000) | (((target - patch - 4) >> 2) & 0xFFFF));
        }
        if ((mode & 0x8001) && (flags & 0x18) == 8) {
            const uint32_t target_pc = pc + ((record - RECORD_BASE) >> 2);
            const uint32_t body = rp_u32(c, record + 4);
            uint32_t published = body;
            if (mode & 2) {
                out = nearby_invalidation_stub(c, out);
                published = out;
                rp_w32(c, c->gp + 0xB58, 0x80000000);
                rp_w32(c, c->gp + 0xB5C, 0);
                const uint32_t words = mode & 0x10 ? 2 : (end - record) >> 4;
                out = rp_emit_ram_guard(c, target_pc, out, words);
                out = rp_emit_jump_delay(c, out, body + UINT32_C(0x30000000));
            }
            rp_w32(c, 0x09C00000 + (target_pc & 0x1FFFFC), published);
        }
        memcpy(rp_memory(c, record, 16), c->vfpu_reset_rows[3], 16);
    }
    memcpy(rp_memory(c, end, 16), c->vfpu_reset_rows[3], 16);
    if (half(c, c->gp + 0xB42))
        rp_w32(c, c->gp + 0x1B0, rp_u32(c, c->gp + 0x1B0) - ((end - RECORD_BASE) >> 4));
    if (!(mode & 0x8001)) rp_w32(c, length_slot, (end - RECORD_BASE) >> 2);
    rp_w32(c, c->gp + 0x1CC, out);
    rp_w32(c, 0x09C00000 + (pc & 0x1FFFFC), entry);
    if (pc == 0x80000080) rp_w32(c, c->gp + 0x1D4, entry);
    rp_event(c, "milestone", "POPS_RAM_block_published", entry, out - entry);
    return entry;
}

uint32_t rp_pops_compile_ram_block(rp_context *c, uint32_t pc)
{
    if (((pc >> 23) & 63) != 0) rp_block(c, "RAM_compiler_address_outside_RAM", pc);
    if (rp_u32(c, c->gp + 0x1CC) > 0x097B0004) rp_pops_invalidate_ram_code(c);
    uint32_t out = rp_pops_prepare_compile(c, pc);
    rp_emit_init_registers(c, out);
    out = nearby_invalidation_stub(c, out);
    const uint32_t length_slot = out;
    if (!(half(c, c->gp + 0xB40) & 0x8001)) out += 4;
    const uint32_t entry = out;
    rp_w32(c, c->gp + 0x740, entry);
    rp_event(c, "compiler_profile", "RAM_mode_and_entry", half(c, c->gp + 0xB40), entry);
    if (!(half(c, c->gp + 0xB40) & 0x8000))
        out = rp_emit_ram_guard(c, pc, out, (rp_u32(c, c->gp + 0xB4C) + 16 - RECORD_BASE) >> 4);
    out = rp_pops_emit_block_records(c, out);
    return publish_ram(c, pc, entry, length_slot, out);
}
