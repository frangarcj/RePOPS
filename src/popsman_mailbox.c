#include "popsman_mailbox.h"

/* +0x3590..+0x35AB: SRL is unsigned; do not replace it with signed division. */
uint32_t repops_pm_c93c56f8(const repops_pm_bus *bus, uint32_t argument)
{
    bus->write32(bus->context, UINT32_C(0xBFC007F4), argument >> 5);
    bus->sync(bus->context);
    return 0;
}

/* +0x35AC..+0x35D7: reproduce the observed bit check, not a guessed pointer API. */
uint32_t repops_pm_0babd960(const repops_pm_bus *bus, uint32_t argument, uint32_t guest_k1)
{
    uint32_t mask = guest_k1 << 11;
    if ((mask & argument & UINT32_C(0x80000000)) != 0) {
        return UINT32_C(0x80000023);
    }
    bus->write32(bus->context, UINT32_C(0xBFC007F8), argument + UINT32_C(2));
    return 0;
}

/* +0x3ACC..+0x3ADB: the store occurs in the JR delay slot. No defined return. */
void repops_pm_e7f06e2b(const repops_pm_bus *bus, uint32_t argument)
{
    bus->write32(bus->context, UINT32_C(0xBD40010C), argument & UINT32_C(0x1FFFFFFF));
}
