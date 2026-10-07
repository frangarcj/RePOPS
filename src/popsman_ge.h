#ifndef REPOPS_POPSMAN_GE_H
#define REPOPS_POPSMAN_GE_H
#include <stdint.h>

/* Host operations for the hash-pinned ARK 6.60 provider at +0x3A00.
 * Numeric guest addresses are never cast to host pointers. All callbacks
 * are required; completing a list is the backend's responsibility. */
typedef struct repops_pm_ge_bus {
    void *context;
    uint32_t (*suspend_interrupts)(void *);
    uint32_t (*read_epc)(void *);
    void (*cache_operation)(void *, uint32_t operation, uint32_t address);
    uint32_t (*read32)(void *, uint32_t address);
    void (*write32)(void *, uint32_t address, uint32_t value);
    void (*resume_interrupts_sync)(void *, uint32_t saved_state);
    uint32_t (*enqueue)(void *, uint32_t start, uint32_t stall, int32_t callback_id,
                        uint32_t arguments);
    uint32_t (*list_sync)(void *, uint32_t list_id, uint32_t mode);
} repops_pm_ge_bus;

enum {
    REPOPS_PM_GE_STALL = 0xBD40010C,
    REPOPS_PM_GE_COMPLETION_STATUS = 0xBD400304,
    REPOPS_PM_GE_ACK = 0xBD400310,
    REPOPS_PM_GE_COMPLETION_BIT = 4,
    REPOPS_PM_GE_POLL_LIMIT = 20
};
uint32_t repops_pm_7014c540(const repops_pm_ge_bus *, uint32_t old_list,
                           uint32_t continuation);
#endif
