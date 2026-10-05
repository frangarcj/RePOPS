#ifndef REPOPS_ME_REGISTRATION_H
#define REPOPS_ME_REGISTRATION_H

#include <stdint.h>

/* Offsets in the pinned, base-zero POPSMAN image; never native pointers. */
#define REPOPS_ME_STACK_HI_WORD UINT32_C(0x2F2C)
#define REPOPS_ME_STACK_LO_WORD UINT32_C(0x2F30)
#define REPOPS_ME_CALLBACK_SLOT UINT32_C(0x4C5C)

/* Required callbacks. start() represents the +0x35D8 boundary, not an
 * implementation of Media Engine hardware. It must return for this model to
 * return. The observed K1 at that boundary is the caller's K1 shifted by 11.
 * Operations are serialized; device timing and concurrent calls are excluded.
 */
typedef struct repops_me_registration_host {
    void *context;
    uint32_t (*read32)(void *, uint32_t module_offset);
    void (*write32)(void *, uint32_t module_offset, uint32_t value);
    void (*start)(void *, uint32_t shifted_k1);
} repops_me_registration_host;

uint32_t repops_me_register(const repops_me_registration_host *host,
                           uint32_t entry, uint32_t stack, uint32_t guest_k1);

#endif
