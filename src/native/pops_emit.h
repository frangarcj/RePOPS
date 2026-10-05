#ifndef REPOPS_POPS_EMIT_H
#define REPOPS_POPS_EMIT_H
#include "pops_ir.h"
#include "runtime.h"

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
#endif
