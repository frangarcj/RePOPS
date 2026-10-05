#ifndef REPOPS_ME_STARTUP_H
#define REPOPS_ME_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

/* Service IDs are import-stub offsets in the pinned provider, not Sony names.
 * Only the declared arguments are part of this reconstructed contract.
 */
enum repops_me_service {
    REPOPS_ME_AVC_RESET = 0x3E04,
    REPOPS_ME_RESET_ENABLE = 0x3E1C,
    REPOPS_ME_BUS_CLOCK = 0x3E0C,
    REPOPS_ME_SUSPEND_INTR = 0x3B84,
    REPOPS_ME_RESUME_INTR = 0x3B8C,
    REPOPS_ME_COPY = 0x3C4C,
    REPOPS_ME_CACHE_WRITEBACK = 0x3CA4,
    REPOPS_ME_DDR_FLUSH = 0x3CFC,
    REPOPS_ME_RESET_RELEASE = 0x3E2C,
    REPOPS_ME_DELAY = 0x3C84,
    REPOPS_ME_CODEC_376399B6 = 0x3CEC
};

typedef struct repops_me_startup_host {
    void *context;
    uint32_t (*read32)(void *, uint32_t address);
    void (*write32)(void *, uint32_t address, uint32_t value);
    uint32_t (*service)(void *, uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2);
} repops_me_startup_host;

enum repops_me_wait_phase {
    REPOPS_ME_FIRST_READ, REPOPS_ME_DELAY_THEN_READ, REPOPS_ME_ACKNOWLEDGED
};

typedef struct repops_me_wait {
    uint32_t target;
    uint32_t previous_ack_bit;
    uint32_t phase;
} repops_me_wait;

/* All callbacks must be valid. Module addresses are base-zero reference
 * offsets; hardware addresses are guest numbers, never native pointers.
 * Service implementations must preserve the caller's guest K1.
 */
void repops_me_boot_begin(const repops_me_startup_host *, repops_me_wait *);
void repops_me_control_begin(const repops_me_startup_host *, uint32_t argument,
                             repops_me_wait *);

/* Cooperative equivalent of one original polling iteration. false is pending,
 * NOT a PSP error or a timeout; the source loops indefinitely without an ack.
 * A control operation returns previous_ack_bit only once this returns true.
 */
bool repops_me_wait_step(const repops_me_startup_host *, repops_me_wait *);

#endif
