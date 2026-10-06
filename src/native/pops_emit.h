#ifndef REPOPS_POPS_EMIT_H
#define REPOPS_POPS_EMIT_H
#include "pops_ir.h"
#include "runtime.h"
#include "pops_state.h"

/* Selected compiler wire state. Saved slots are not a dense 32-GPR array;
 * the original normalized register encoding selects the backing slot. */
typedef struct {
    uint8_t earlier_state[0x180];
    uint32_t saved_register_slots[8];
    uint8_t unknown_1a0[0x740 - 0x1A0];
    uint32_t block_begin;
    uint8_t unknown_744[0x751 - 0x744];
    uint8_t hilo_cached, hilo_dirty, unknown_753;
    uint8_t temporary_dirty[12], temporary_guest[12], temporary_host[12];
    int8_t register_location[32];
    uint8_t unknown_798[0xB48 - 0x798];
    uint32_t source_address_bias, last_analysis_record, analysis_base_pc, analysis_end_pc;
    uint32_t known_register_mask, known_register_values[32];
} rp_core_emit_layout;
#define RP_EMIT_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_emit_layout, member)
_Static_assert(offsetof(rp_core_emit_layout, temporary_guest) == 0x760, "emitter temporary owners");
_Static_assert(offsetof(rp_core_emit_layout, hilo_cached) == 0x751, "emitter HI/LO cache");
_Static_assert(offsetof(rp_core_emit_layout, hilo_dirty) == 0x752, "emitter HI/LO dirty mask");
_Static_assert(offsetof(rp_core_emit_layout, register_location) == 0x778, "emitter register locations");
_Static_assert(offsetof(rp_core_emit_layout, known_register_mask) == 0xB58, "emitter known mask");
_Static_assert(offsetof(rp_core_emit_layout, last_analysis_record) == 0xB4C, "analysis record ceiling");
_Static_assert(offsetof(rp_core_emit_layout, known_register_values) == 0xB5C, "emitter known values");

/* Memory/COP analysis view only. Other compiler phases reuse some of these
 * bytes for emitted-code and patch addresses; never cast guest backing. */
typedef struct {
    uint16_t flags;
    uint8_t destination, opcode;
    uint16_t category, cost;
    int16_t displacement;
    uint8_t unused_payload[2];
    int8_t base_register;
    uint8_t source_register, cop_register, auxiliary;
} rp_cop_memory_record_layout;
#define RP_COP_RECORD_ADDRESS(record, member) RP_FIELD_ADDRESS(record, rp_cop_memory_record_layout, member)
enum {
    RP_COP_WRITE_POLICY_TABLE = 0xD42FC,
    /* Encoded host registers, not normalized opcodes or emitter selectors. */
    RP_EMIT_HOST_V0 = 0x82, RP_EMIT_HOST_A1 = 0x85
};
_Static_assert(sizeof(rp_cop_memory_record_layout) == 16, "COP memory record stride");
_Static_assert(offsetof(rp_cop_memory_record_layout, displacement) == 8, "COP displacement");
_Static_assert(offsetof(rp_cop_memory_record_layout, cop_register) == 14, "COP register selector");

typedef struct {
    uint16_t flags;
    uint8_t destination, opcode;
    uint16_t category, cost;
    uint32_t command;
    uint8_t sources[4];
} rp_gte_record_layout;
#define RP_GTE_RECORD_ADDRESS(record, member) RP_FIELD_ADDRESS(record, rp_gte_record_layout, member)
_Static_assert(sizeof(rp_gte_record_layout) == 16, "GTE analysis record stride");
_Static_assert(offsetof(rp_gte_record_layout, command) == 8, "GTE command payload");
enum {
    RP_ANALYSIS_RECORD_BASE = 0x041B0000,
    RP_RECORD_DELAY_SLOT = 1, RP_RECORD_LOCAL_TARGET = 4,
    RP_RECORD_EXIT = 0x10, RP_RECORD_GTE_FLAGS_OVERWRITTEN = 0x2000
};

/* All cursors/words are guest numeric addresses and Allegrex instructions.
 * None of these functions creates callable host machine code.
 */
void rp_emit_init_registers(rp_context *, uint32_t);
int32_t rp_emit_lookup_register(rp_context *, uint32_t);
uint32_t rp_emit_previous_movable(rp_context *, uint32_t);
uint32_t rp_emit_load_register(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_constant(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_spill_slot(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_temp(rp_context *, uint32_t, uint32_t);
void rp_emit_release_temp(rp_context *, uint32_t);
uint32_t rp_emit_flush_hilo(rp_context *, uint32_t);
uint32_t rp_emit_flush_registers(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_allocate(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_jump_delay(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_pair(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *);
uint32_t rp_emit_immediate(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_debit(rp_context *, int32_t, uint32_t);
uint32_t rp_emit_load_state(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_known_value(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_record(rp_context *, rp_pops_category, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_spill_all(rp_context *, uint32_t);
uint32_t rp_emit_argument(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_result(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_constant_read(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_fixed_memory(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_memory(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_memory_record(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_store_state(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_emit_exit_target(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_capture_branch(rp_context *, uint32_t, uint32_t);
uint32_t rp_emit_conditional_branch(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_emit_block_records(rp_context *, uint32_t);
uint32_t rp_pops_publish_bios_block(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_compile_bios_block(rp_context *, uint32_t);
uint32_t rp_emit_ram_guard(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_compile_ram_block(rp_context *, uint32_t);
#endif
