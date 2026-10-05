#include "me_registration.h"

/* POPSMAN +0x3490..+0x3513, sceMeAudio_DE630CD2.
 * This is the observed bit check, not a general-purpose pointer validator.
 */
uint32_t repops_me_register(const repops_me_registration_host *host,
                           uint32_t entry, uint32_t stack, uint32_t guest_k1)
{
    const uint32_t mask = guest_k1 << 11;
    if ((mask & entry & UINT32_C(0x80000000)) != 0 ||
        (mask & stack & UINT32_C(0x80000000)) != 0) {
        return UINT32_C(0x80000023);
    }

    const uint32_t hi = host->read32(host->context, REPOPS_ME_STACK_HI_WORD);
    const uint32_t lo = host->read32(host->context, REPOPS_ME_STACK_LO_WORD);
    host->write32(host->context, REPOPS_ME_CALLBACK_SLOT, entry);

    /* Preserve both ORs and store order. The high-word write is in the
     * original JAL delay slot. Re-registration does not clear old immediates.
     */
    host->write32(host->context, REPOPS_ME_STACK_LO_WORD,
                  lo | (stack & UINT32_C(0xFFFF)));
    host->write32(host->context, REPOPS_ME_STACK_HI_WORD, hi | (stack >> 16));
    host->start(host->context, mask);
    return 0;
}
