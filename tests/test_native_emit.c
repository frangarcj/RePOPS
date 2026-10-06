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
    /* Signed-half policy preserves the live source and stores a sign-extended
     * word, using the original distinct GPR, saved-slot and FPR sequences. */
    rp_emit_init_registers(c, cursor);
    uint32_t half_start = cursor;
    cursor = rp_emit_store_state(c, RP_STATE_STORE_SIGNED_HALF_WORD, 8, 0x11C, cursor);
    assert(cursor == half_start + 8);
    assert(rp_u32(c, half_start) == (0x7C000620 | (16u << 16) | (8u << 11)));
    assert(rp_u32(c, half_start + 4) == 0xAF88011C);
    assert(rp_emit_lookup_register(c, 8) == 16);
    assert(*(uint8_t *)rp_memory(c, RP_EMIT_ADDRESS(c, temporary_guest[0]), 1) == 0);
    rp_emit_init_registers(c, cursor);
    half_start = cursor;
    cursor = rp_emit_store_state(c, RP_STATE_STORE_SIGNED_HALF_WORD, 13, 0x11C, cursor);
    assert(cursor == half_start + 8 && rp_u32(c, half_start) == 0x87880194);
    assert(rp_u32(c, half_start + 4) == 0xAF88011C);
    rp_emit_init_registers(c, cursor);
    half_start = cursor;
    cursor = rp_emit_store_state(c, RP_STATE_STORE_SIGNED_HALF_WORD, 10, 0x11C, cursor);
    assert(cursor == half_start + 12 && rp_u32(c, half_start) == 0x4408A000);
    assert(rp_u32(c, half_start + 4) == (0x7C000620 | (8u << 16) | (8u << 11)));
    assert(rp_u32(c, half_start + 8) == 0xAF88011C);
    for (unsigned already_signed = 0; already_signed < 2; ++already_signed) {
        rp_emit_init_registers(c, cursor);
        rp_w32(c, RP_EMIT_ADDRESS(c, known_register_mask), 0x80800000);
        const uint32_t value = already_signed ? UINT32_C(0xFFFF8001) : 0x8001;
        rp_w32(c, RP_EMIT_ADDRESS(c, known_register_values[8]), value);
        half_start = cursor;
        cursor = rp_emit_store_state(c, RP_STATE_STORE_SIGNED_HALF_WORD, 8, 0x11C, cursor);
        assert(cursor == half_start + (already_signed ? 4 : 8));
        assert(rp_u32(c, cursor - 4) == (already_signed ? 0xAF90011C : 0xAF88011C));
        assert(rp_u32(c, RP_EMIT_ADDRESS(c, known_register_values[8])) == value);
    }
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
    /* First stack access classifies A0; a following access reuses that base. */
    mapping[29] = 30;
    rp_emit_init_registers(c, cursor);
    const uint32_t stack_start = cursor;
    cursor = rp_emit_memory(c, RP_OP_SW, 8, 29, 0x14, cursor, 0);
    assert(rp_u32(c, stack_start) == 0x27C40014);
    assert(rp_u32(c, stack_start + 4) == 0x7C8805C0);
    assert(rp_u32(c, cursor - 4) == 0xAC900000);
    const uint32_t stack_reuse = cursor;
    cursor = rp_emit_memory(c, RP_OP_SW, 8, 29, 0x18, cursor, 0);
    assert(cursor == stack_reuse + 4 && rp_u32(c, stack_reuse) == 0xAC900004);
    cursor = rp_emit_memory(c, RP_OP_LW, 10, 29, 0x1C, cursor, 1);
    assert(rp_u32(c, cursor - 4) == ((0x31u << 26) | (4u << 21) | (20u << 16) | 8));
    cursor = rp_emit_record(c, RP_CAT_JUMP_REGISTER, record, cursor, 3);
    const uint32_t indirect = rp_u32(c, record + 12);
    assert(rp_u32(c, indirect) == (0x30002648u >> 2));
    assert(rp_u32(c, indirect + 4) == 0x2739FFFD && cursor == indirect + 8);
    assert(rp_pops_constant_read(c, 0x1F000084, 0) == 0);
    assert(rp_pops_constant_read(c, 0x1F802030, 0) == 0xFFFF);
    assert(rp_pops_constant_read(c, 0x1F802030, 4) == 0xFF);
    assert(rp_pops_constant_read(c, 0x1F802030, 5) == UINT32_MAX);
    rp_emit_init_registers(c, cursor);
    memset(rp_memory(c, record, 16), 0, 16);
    rp_w8(c, record + 2, 1); rp_w8(c, record + 14, 0x4C);
    cursor = rp_emit_record(c, RP_CAT_READ_COP, record, cursor, 2);
    assert(rp_u32(c, cursor - 4) == 0x8F920130);
    const uint32_t rfe = cursor;
    cursor = rp_emit_record(c, RP_CAT_COP0_CONTROL, record, cursor, 2);
    assert(rp_u32(c, rfe) == 0x8F850130 && rp_u32(c, rfe + 4) == 0x00053082);
    assert(rp_u32(c, rfe + 8) == 0x7CC51804 && rp_u32(c, rfe + 12) == 0xAF850130);
    assert(rp_u32(c, cursor - 12) == 0x0C0025AB);
    rp_emit_init_registers(c, cursor);
    memset(rp_memory(c, record, 32), 0, 32);
    rp_w8(c, record + 2, 1); rp_w8(c, record + 3, RP_OP_SRL); rp_w8(c, record + 13, 8);
    rp_w32(c, record + 8, 1u << 6);
    rp_w32(c, c->gp + 0xB58, 0x80800000);
    rp_w32(c, c->gp + 0xB5C + 8 * 4, 0x80000001);
    cursor = rp_emit_record(c, RP_CAT_SHIFT_IMMEDIATE, record, cursor, 2);
    assert(rp_u32(c, cursor - 4) == ((16u << 16) | (18u << 11) | (1u << 6) | 2));
    assert(rp_u32(c, c->gp + 0xB5C + 4) == 0x40000000);
    rp_emit_init_registers(c, cursor);
    memset(rp_memory(c, record, 32), 0, 32);
    rp_w32(c, record, 0x8000); rp_w8(c, record + 2, 1);
    rp_w8(c, record + 3, RP_OP_SLL); rp_w8(c, record + 13, 8);
    rp_w32(c, record + 8, 24u << 6);
    rp_w8(c, record + 18, 1); rp_w8(c, record + 19, RP_OP_SRA);
    rp_w8(c, record + 20, RP_CAT_SHIFT_IMMEDIATE);
    rp_w32(c, record + 24, 24u << 6); rp_w8(c, record + 29, 1);
    cursor = rp_emit_record(c, RP_CAT_SHIFT_IMMEDIATE, record, cursor, 2);
    assert(rp_u32(c, cursor - 4) == (0x7C000420 | (16u << 16) | (18u << 11)));
    assert(*(uint8_t *)rp_memory(c, record + 20, 1) == RP_CAT_EMPTY);
    rp_emit_init_registers(c, cursor);
    memset(rp_memory(c, record, 32), 0, 32);
    rp_w8(c, record + 3, RP_OP_DIV); rp_w8(c, record + 12, 8);
    rp_w8(c, record + 13, 9);
    cursor = rp_emit_record(c, RP_CAT_MULT_DIV, record, cursor, 2);
    assert(rp_u32(c, cursor - 4) == ((16u << 21) | (17u << 16) | 0x1A));
    assert(*(uint8_t *)rp_memory(c, c->gp + 0x751, 1) == 3);
    rp_w8(c, record + 2, 1); rp_w8(c, record + 3, RP_OP_MFLO);
    cursor = rp_emit_record(c, RP_CAT_READ_HILO, record, cursor, 2);
    assert(rp_u32(c, cursor - 8) == ((18u << 11) | 0x12));
    assert(rp_u32(c, cursor - 4) == (0xAF8001A8 | (18u << 16)));
    assert(*(uint8_t *)rp_memory(c, c->gp + 0x752, 1) == 1);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[2].bytes); free(c);
    puts("Emitter smoke: FPR/memory locations, temporary state, constants and debit passed; not exhaustive equivalence.");
    return 0;
}
