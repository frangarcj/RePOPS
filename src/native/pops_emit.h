#ifndef REPOPS_POPS_EMIT_H
#define REPOPS_POPS_EMIT_H
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
uint32_t rp_emit_record(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
#endif
