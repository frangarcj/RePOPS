#ifndef REPOPS_POPSMAN_MAILBOX_H
#define REPOPS_POPSMAN_MAILBOX_H

#include <stdint.h>

/* Observed guest addresses, never native pointers. Both callbacks are required.
 * These models reproduce three leaf routines from the hash-pinned ARK reference.
 * No PSP device, concurrent memory ordering, or audio/graphics output is emulated.
 */
typedef struct repops_pm_bus {
    void *context;
    void (*write32)(void *, uint32_t guest_address, uint32_t value);
    void (*sync)(void *);
} repops_pm_bus;

uint32_t repops_pm_c93c56f8(const repops_pm_bus *bus, uint32_t argument);
uint32_t repops_pm_0babd960(const repops_pm_bus *bus, uint32_t argument, uint32_t guest_k1);
void repops_pm_e7f06e2b(const repops_pm_bus *bus, uint32_t argument);

#endif
