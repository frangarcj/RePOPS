#ifndef REPOPS_NATIVE_ME_WORKER_H
#define REPOPS_NATIVE_ME_WORKER_H
#include <stdbool.h>
#include <stdint.h>

/* Cooperative reconstruction of corpus POPSMAN +0x2F88, not a PSP device
 * emulator. Provider offsets and absolute bus addresses have separate APIs.
 * All callbacks are required. sample() may refuse an unreconstructed producer;
 * it must not mutate producer state on refusal. No host pointer is a guest PC.
 */
typedef enum rp_me_service {
    RP_ME_SYNC, RP_ME_DCACHE_WRITEBACK_INVALIDATE, RP_ME_DDR_FLUSH
} rp_me_service;

typedef struct rp_me_bus {
    void *context;
    uint32_t (*provider_read32)(void *, uint32_t module_offset);
    uint32_t (*read32)(void *, uint32_t guest_address);
    uint16_t (*read16)(void *, uint32_t guest_address);
    void (*write32)(void *, uint32_t guest_address, uint32_t value);
    void (*service)(void *, rp_me_service, uint32_t argument);
    bool (*sample)(void *, uint32_t guest_entry, uint32_t *packed_result);
} rp_me_bus;

typedef enum rp_me_phase {
    RP_ME_ENTRY, RP_ME_RESET_OUTPUT, RP_ME_WAIT_IDLE, RP_ME_WAIT_CONFIG,
    RP_ME_PREFILL, RP_ME_PUMP, RP_ME_EMIT_PREVIOUS, RP_ME_CALL_SAMPLE,
    RP_ME_DRAIN, RP_ME_STOPPED
} rp_me_phase;

typedef struct rp_me_worker {
    rp_me_phase phase;
    uint32_t callback, packed, prefill_remaining;
    int32_t low_sample, high_sample;
} rp_me_worker;

typedef enum rp_me_step_result {
    RP_ME_PROGRESS, RP_ME_WAITING_IO, RP_ME_CALLBACK_RETURNED,
    RP_ME_CALLBACK_UNAVAILABLE, RP_ME_ACK_WRITTEN, RP_ME_PARKED,
    RP_ME_INVALID_HOST
} rp_me_step_result;

void rp_me_worker_init(rp_me_worker *worker);
rp_me_step_result rp_me_worker_step(rp_me_worker *worker, const rp_me_bus *bus);
#endif
