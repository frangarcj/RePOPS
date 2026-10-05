#include "runtime.h"
#include "../me_registration.h"

/* A nonzero logical identity for POPS offset zero in this native harness.
 * It is not a measured PSP load address and is never executed or dereferenced.
 */
#define CALLBACK_ID UINT32_C(0x08800000)

static uint32_t provider_read(void *ctx, uint32_t offset)
{
    rp_context *c = ctx;
    switch (offset) {
    case REPOPS_ME_STACK_HI_WORD: return c->me_stack_hi;
    case REPOPS_ME_STACK_LO_WORD: return c->me_stack_lo;
    case REPOPS_ME_CALLBACK_SLOT: return c->me_callback;
    default: rp_block(c, "native_me_provider_read_unknown", offset);
    }
}
static void provider_write(void *ctx, uint32_t offset, uint32_t value)
{
    rp_context *c = ctx;
    switch (offset) {
    case REPOPS_ME_STACK_HI_WORD: c->me_stack_hi = value; break;
    case REPOPS_ME_STACK_LO_WORD: c->me_stack_lo = value; break;
    case REPOPS_ME_CALLBACK_SLOT: c->me_callback = value; break;
    default: rp_block(c, "native_me_provider_write_unknown", offset);
    }
}

/* This sink deliberately has immediate capacity and no device interrupts.
 * It lets the reconstructed worker reach the real sample producer boundary;
 * it is not an implementation of the PSP audio hardware.
 */
static uint32_t bus_read(void *ctx, uint32_t address)
{
    rp_context *c = ctx;
    switch (address) {
    case 0xBFC007F0: return c->me_ack;
    case 0xBFC007F4: return c->me_value;
    case 0xBFC007F8: return c->me_request;
    case 0xBE000028: return 0x20;
    case 0xBC000044: case 0xBC00000C:
    case 0xBE00000C: case 0xBE000050: return 0;
    default: rp_block(c, "native_me_sink_read_unknown", address);
    }
}
static uint16_t bus_read_half(void *ctx, uint32_t address)
{
    const uint8_t *p = rp_memory(ctx, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void bus_write(void *ctx, uint32_t address, uint32_t value)
{
    rp_context *c = ctx;
    if (address == 0xBFC007F0) {
        c->me_ack = value;
        rp_event(c, "reconstructed_provider", "me_worker_ack", address, value);
    } else if (address == 0xBE000070) {
        ++c->me_output_words;
        c->me_last_output = value;
    } else if ((address >= 0xBE000000 && address < 0xBE000074) ||
               (address >= 0xBC000000 && address <= 0xBC000044) ||
               address == 0xBC200000 || address == 0xBC200004 ||
               address == 0xBC300008 || address == 0xBFC00650) {
        rp_event(c, "headless_adapter", "me_output_register", address, value);
    } else rp_block(c, "native_me_sink_write_unknown", address);
}
static void bus_service(void *ctx, rp_me_service service, uint32_t argument)
{
    rp_context *c = ctx;
    rp_event(c, "headless_adapter", "me_cache_or_bus_service", (uint32_t)service, argument);
}
static bool sample(void *ctx, uint32_t entry, uint32_t *packed)
{
    rp_context *c = ctx; (void)packed;
    if (entry != CALLBACK_ID) rp_block(c, "unknown_native_me_callback", entry);
    rp_event(c, "milestone", "me_worker_reached_pops_sample_callback", 0, c->me_output_words);
    /* Never replace an unimplemented mixer with a fabricated successful sample. */
    return false;
}
static void start_worker(void *ctx, uint32_t shifted_k1)
{
    rp_context *c = ctx;
    rp_event(c, "host_adapter", "native_ME_start_instead_of_hardware_bootstrap", 0x35D8, shifted_k1);
    const uint32_t stack = (c->me_stack_hi << 16) | (c->me_stack_lo & 0xFFFF);
    rp_event(c, "milestone", "me_callback_registered_logical_identity", c->me_callback, stack);
    c->me_request = 1;
    c->me_ack = 0;
    c->me_output_words = 0;
    rp_me_worker_init(&c->me_worker);
    const rp_me_bus bus = {c, provider_read, bus_read, bus_read_half, bus_write, bus_service, sample};
    for (unsigned step = 0; step < 128; ++step) {
        switch (rp_me_worker_step(&c->me_worker, &bus)) {
        case RP_ME_CALLBACK_UNAVAILABLE:
            rp_event(c, "state", "me_ack_before_unimplemented_sample", 0xBFC007F0, c->me_ack);
            rp_block(c, "pops_me_sample_callback_not_reconstructed", 0);
        case RP_ME_INVALID_HOST: rp_block(c, "native_me_host_contract_invalid", 0x2F88);
        case RP_ME_PARKED: rp_block(c, "native_me_worker_parked", 0x2F88);
        default: break;
        }
        if (c->me_ack == 1) return;
    }
    rp_block(c, "native_me_start_still_pending", 0x35D8);
}

void rp_pops_start_me(rp_context *c)
{
    if (!c->me_callback) {
        c->me_stack_hi = 0x3C1D0000;
        c->me_stack_lo = 0x37BD0000;
    }
    const repops_me_registration_host host = {c, provider_read, provider_write, start_worker};
    rp_event(c, "reconstructed_provider", "me_register_DE630CD2", 0x3490, CALLBACK_ID);
    (void)repops_me_register(&host, CALLBACK_ID, 0x09FF8000, 0);
}
