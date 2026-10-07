#include "pops_gpu.h"
#include "../popsman_ge.h"

/* Single-threaded capture adapter for the recovered provider. With no GE
 * executor there is no completion bit: take the original fallback, never
 * manufacture its fast-path acknowledgement. A pixel-dependent wait stops. */
typedef struct {
    rp_context *c;
    bool require_pixels;
    unsigned cache_probes;
} ge_capture_host;

static uint32_t suspend_interrupts(void *context)
{
    (void)context;
    return 0; /* No PSP interrupt controller on the coherent capture host. */
}
static uint32_t cache_epc(void *context)
{
    (void)context;
    return 0; /* Only used by the elided cache operation, never dereferenced. */
}
static void cache_operation(void *context, uint32_t operation, uint32_t address)
{
    ge_capture_host *host = context;
    (void)address;
    if (operation != 0xA) rp_block(host->c, "GE_host_cache_operation_unknown", operation);
    ++host->cache_probes;
}
static uint32_t read32(void *context, uint32_t address)
{
    ge_capture_host *host = context;
    if (address != REPOPS_PM_GE_COMPLETION_STATUS)
        rp_block(host->c, "GE_host_register_read_unknown", address);
    return 0; /* No execution, hence no completion; this is not a device model. */
}
static void write32(void *context, uint32_t address, uint32_t value)
{
    ge_capture_host *host = context;
    rp_context *c = host->c;
    if (address == REPOPS_PM_GE_STALL) {
        c->ge_stalled_list = value;
        rp_event(c, "headless_adapter", "POPSMAN_GE_stall_write_not_executed", address, value);
    } else if (address == REPOPS_PM_GE_ACK) {
        rp_block(c, "GE_capture_has_no_completion_to_acknowledge", address);
    } else {
        rp_w32(c, address, value);
        rp_event(c, "GPU_GE_word", "provider_written_not_executed", address, value);
    }
}
static void resume_interrupts(void *context, uint32_t saved)
{ (void)context; (void)saved; }
static uint32_t enqueue(void *context, uint32_t start, uint32_t stall,
                        int32_t callback_id, uint32_t arguments)
{
    ge_capture_host *host = context;
    rp_context *c = host->c;
    if (start != stall || callback_id != -1 || arguments)
        rp_block(c, "GE_capture_enqueue_contract_unknown", start);
    c->ge_stalled_list = stall;
    rp_event(c, "headless_adapter", "GE_continuation_enqueued_not_executed", start, c->next_id + 1);
    return ++c->next_id;
}
static uint32_t list_sync(void *context, uint32_t id, uint32_t mode)
{
    ge_capture_host *host = context;
    rp_context *c = host->c;
    rp_event(c, "headless_adapter", "POPSMAN_cache_probes_elided_coherent_host", 0xA, host->cache_probes);
    if (mode) rp_block(c, "GE_capture_sync_mode_unknown", mode);
    if (host->require_pixels) {
        rp_event(c, "GPU_readback_boundary", "GE_list_execution_needed_before_pixels", id, c->ge_stalled_list);
        rp_block(c, "GE_backend_execution_required", 0x3A98);
    }
    rp_event(c, "headless_adapter", "GE_list_sync_captured_not_executed", id, mode);
    return 0;
}

uint32_t rp_popsman_ge_finish_host(rp_context *c, uint32_t old_list,
                                  uint32_t continuation, bool require_pixels)
{
    ge_capture_host host = {c, require_pixels, 0};
    const repops_pm_ge_bus bus = {&host, suspend_interrupts, cache_epc,
        cache_operation, read32, write32, resume_interrupts, enqueue, list_sync};
    ++c->services;
    rp_event(c, "native_c_provider", "popsman_ark_7014C540", 0x3A00, old_list);
    const uint32_t next = repops_pm_7014c540(&bus, old_list, continuation);
    rp_event(c, "headless_adapter", "POPSMAN_7014C540_submit_captured_not_rendered", old_list, next);
    return next;
}
