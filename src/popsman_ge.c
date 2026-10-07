#include "popsman_ge.h"

/* +0x3A00..+0x3ACB. This is the provider's control flow, not a GE renderer.
 * The fast path retains the old id. The fallback creates a continuation
 * before waiting for the old id and returns the enqueue result unchanged. */
uint32_t repops_pm_7014c540(const repops_pm_ge_bus *bus, uint32_t old_list,
                           uint32_t continuation)
{
    void *const host = bus->context;
    const uint32_t saved = bus->suspend_interrupts(host);
    const uint32_t physical = continuation & UINT32_C(0x1FFFFFFF);
    const uint32_t uncached = physical | UINT32_C(0x40000000);
    bus->write32(host, uncached - 4, UINT32_C(0x0F000000));
    bus->write32(host, REPOPS_PM_GE_STALL, physical);
    uint32_t epc = bus->read_epc(host);
    for (unsigned i = 0; i < REPOPS_PM_GE_POLL_LIMIT; ++i) {
        bus->cache_operation(host, 0xA, epc + 64);
        const uint32_t status = bus->read32(host, REPOPS_PM_GE_COMPLETION_STATUS);
        epc += 64;
        if (status & REPOPS_PM_GE_COMPLETION_BIT) {
            bus->write32(host, REPOPS_PM_GE_ACK, REPOPS_PM_GE_COMPLETION_BIT);
            bus->resume_interrupts_sync(host, saved);
            return old_list;
        }
    }
    bus->write32(host, uncached, UINT32_C(0x0C000000));
    bus->write32(host, REPOPS_PM_GE_STALL, 0);
    bus->resume_interrupts_sync(host, saved);
    const uint32_t next = bus->enqueue(host, uncached, uncached, -1, 0);
    (void)bus->list_sync(host, old_list, 0);
    return next;
}
