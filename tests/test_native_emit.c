#include "../src/native/pops_emit.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->trace = tmpfile(); assert(c->trace); c->gp = 0x10000;
    c->regions[0] = (rp_region){0, 0x100000, calloc(1, 0x100000)};
    /* The original reload helper reads the preceding word even at the start
     * cursor, so include that surrounding mapped memory in this fixture. */
    c->regions[2] = (rp_region){0x09B7F000, 0x2000, calloc(1, 0x2000)};
    assert(c->regions[0].bytes && c->regions[2].bytes);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected emitter blocker: %s @ 0x%08X\n", c->stop_kind, c->stop_address);
        abort();
    }
    /* Synthetic mapping fixture: tests semantics, not a copied firmware table. */
    memset(rp_module_memory(c, 0xD40C8, 32), 0xFF, 32);
    uint8_t *mapping = rp_module_memory(c, 0xD40C8, 32);
    mapping[0] = 0; mapping[8] = 16; mapping[10] = (uint8_t)-20;
    uint8_t *temps = rp_module_memory(c, 0xD40E8, 12);
    for (unsigned i = 0; i < 12; ++i) temps[i] = (uint8_t)(8 + i);
    const uint32_t start = 0x09B80000;
    rp_emit_init_registers(c, start);
    assert(rp_emit_lookup_register(c, 8) == 16);
    assert(rp_emit_lookup_register(c, 10) == -20);
    assert(rp_emit_lookup_register(c, 13) == -1);

    uint32_t packed = rp_emit_allocate(c, start, 10, 0, 2);
    uint32_t cursor = (packed >> 5) << 2;
    assert(cursor == start + 4 && (packed & 31) == 19);
    assert(rp_u32(c, start) == 0x4413A000); /* MFC1 temporary,F20 */
    assert(rp_emit_lookup_register(c, 10) == 19);
    packed = rp_emit_allocate(c, cursor, 10, 0, 1);
    assert(((packed >> 5) << 2) == cursor);
    cursor = rp_emit_spill_slot(c, 11, cursor);
    assert(rp_u32(c, cursor - 4) == 0x4493A000); /* MTC1 temporary,F20 */
    cursor = rp_emit_flush_registers(c, cursor, 11);
    assert(rp_emit_lookup_register(c, 10) == -20);

    packed = rp_emit_allocate(c, cursor, 13, 0, 2);
    assert((packed & 31) == 19);
    cursor = (packed >> 5) << 2;
    assert(rp_u32(c, cursor - 4) == 0x8F930194); /* LW temporary,guest-slot(GP) */
    cursor = rp_emit_debit(c, 90, cursor);
    assert(rp_u32(c, cursor - 4) == 0x2739FFA6);

    cursor = rp_emit_known_value(c, 8, 0x12345678, cursor, 0);
    const uint32_t before = cursor;
    assert(rp_emit_known_value(c, 8, 0x12345678, cursor, 0) == before);
    cursor = rp_emit_constant(c, cursor, 5, 0x1234567B);
    assert(cursor == before + 4 && rp_u32(c, before) == 0x26050003);

    rp_emit_init_registers(c, cursor);
    for (unsigned i = 0; i < 12; ++i) assert(rp_emit_temp(c, 2, 0) == 8 + i);
    rp_w32(c, c->gp + 0x744, 123);
    assert(rp_emit_temp(c, 2, 0) == 2 && rp_u32(c, c->gp + 0x744) == 0);
    /* Capture S0 before a delay-slot write; the branch consumes that temporary
     * rather than comparing the later value of the guest register. */
    rp_emit_init_registers(c, cursor);
    const uint32_t record = 0x200;
    rp_w8(c, record + 12, 8); rp_w8(c, record + 13, 0);
    cursor = rp_emit_capture_branch(c, record, cursor);
    assert(rp_u32(c, cursor - 4) == ((16u << 21) | (8u << 11) | 0x21));
    cursor = rp_emit_conditional_branch(c, RP_OP_BNE, UINT32_MAX, 0, 7, record + 12, cursor);
    const uint32_t branch = rp_u32(c, record + 12);
    assert(rp_u32(c, branch) == (0x14000000 | (8u << 21)));
    assert(rp_u32(c, branch + 4) == 0x2739FFF9 && cursor == branch + 8);
    rp_emit_init_registers(c, cursor);
    cursor = rp_emit_store_state(c, 0, 0, 0x11C, cursor);
    assert(rp_u32(c, cursor - 4) == 0xAF80011C);
    const uint32_t cause = cursor;
    cursor = rp_emit_store_state(c, 0, 0, 0x134, cursor);
    assert(cursor == cause + 12);
    assert(rp_u32(c, cause) == 0x83850135 && rp_u32(c, cause + 4) == 0x7C050804);
    assert(rp_u32(c, cause + 8) == 0xA3850135);
    /* Dynamic SW selects the original helper and retains argument setup in
     * its JAL delay slot. No emulated bus is substituted by the compiler. */
    rp_emit_init_registers(c, cursor);
    const uint32_t memory_start = cursor;
    cursor = rp_emit_memory(c, RP_OP_SW, 0, 8, 4, cursor, 0);
    assert(rp_u32(c, memory_start) == 0x26040004);
    assert(rp_u32(c, cursor - 8) == 0x0C000914);
    assert(rp_u32(c, cursor - 4) == 0x00002821);
    mapping[1] = 18; mapping[9] = 17;
    rp_emit_init_registers(c, cursor);
    memset(rp_memory(c, record, 16), 0, 16);
    rp_w8(c, record + 2, 1); rp_w8(c, record + 3, RP_OP_SLTU);
    rp_w8(c, record + 12, 8); rp_w8(c, record + 13, 9);
    cursor = rp_emit_record(c, RP_CAT_ALU, record, cursor, 2);
    assert(rp_u32(c, cursor - 4) == ((16u << 21) | (17u << 16) | (18u << 11) | 0x2B));
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[2].bytes); free(c);
    puts("Emitter smoke: FPR/memory locations, temporary state, constants and debit passed; not exhaustive equivalence.");
    return 0;
}
