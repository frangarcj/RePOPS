#include <stdint.h>

/* POPS +0x44DC: emit ADDIU T9,T9,(-amount & 0xffff) for positive amount.
 * The emitted register/opcode is observed; a cycle-counter role is unproven.
 * The output pointer must be writable/aligned when amount is positive.
 */
uint32_t *repops_emit_t9_decrement(int32_t amount, uint32_t *output)
{
    if (amount > 0) {
        *output++ = UINT32_C(0x27390000) |
                    ((UINT32_C(0) - (uint32_t)amount) & UINT32_C(0xFFFF));
    }
    return output;
}
