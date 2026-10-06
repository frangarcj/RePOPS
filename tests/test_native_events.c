#include "../src/native/runtime.h"
#include "../src/native/pops_timer.h"
#include "../src/native/pops_dma.h"
#include "../src/native/pops_serial.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned callbacks;
static bool resume_fixture;
static uint32_t resume_result, resume_address, resume_bytes, resume_control;
static unsigned resume_calls, ready_calls;
uint32_t rp_pops_gpu_dma_transfer(rp_context *c, uint32_t a, uint32_t n, uint32_t f)
{
    assert(resume_fixture);
    ++resume_calls; resume_address = a; resume_bytes = n; resume_control = f;
    if ((int32_t)resume_result < 0)
        rp_pops_schedule_event(c, RP_DMA_ADDRESS(c, channels[2]), 7);
    return resume_result;
}
uint32_t rp_pops_cd_dma_transfer(rp_context *c, uint32_t a, uint32_t n, uint32_t f)
{ (void)c; (void)a; (void)n; (void)f; abort(); }
void rp_pops_cd_event(rp_context *c, uint32_t event, uint32_t callback)
{ (void)c; (void)event; (void)callback; abort(); }

/* One scripted callback isolates the scheduler's unlink and time accounting. */
void rp_pops_graphics_event(rp_context *c, uint32_t callback)
{
    if (resume_fixture && callback == 0x125F0) { ++ready_calls; return; }
    const uint32_t head = c->gp + 0x1B8, event = c->gp + 0x280;
    assert(callback == 0x1265C);
    assert(rp_u32(c, head) == head && rp_u32(c, head + 4) == head);
    assert(!rp_u32(c, event + 4) && !rp_u32(c, c->gp + 0x1B0));
    ++callbacks;
    rp_w32(c, head, event); rp_w32(c, head + 4, event);
    rp_w32(c, event, head); rp_w32(c, event + 4, head);
    rp_w32(c, event + 8, 200);
    rp_w32(c, c->gp + 0x1B0, (uint32_t)-3);
}
void rp_pops_initialize_core(rp_context *c) { (void)c; abort(); }
void rp_pops_invalidate_ram_code(rp_context *c) { (void)c; abort(); }
void rp_pops_prepare_exception(rp_context *c, uint32_t v) { (void)c; (void)v; abort(); }

/* Scripted transfer return isolates the resumed-channel contract. The real
 * linked GPU callback is exercised by the integrated FFVI diagnostic. */
static void check_dma_resume(rp_context *c)
{
    const uint32_t node = RP_DMA_ADDRESS(c, channels[2]);
    const uint32_t head = RP_CORE_CLOCK_ADDRESS(c, event_head_next);
    const uint32_t deadline = RP_CORE_CLOCK_ADDRESS(c, event_deadline);
    const uint32_t addr = RP_DMA_REGISTER(c, 2, address);
    const uint32_t block = RP_DMA_REGISTER(c, 2, block_control);
    const uint32_t control = RP_DMA_REGISTER(c, 2, channel_control);
    const uint32_t callback = RP_DMA_CHANNEL(c, 2, event.callback);
    resume_fixture = true; resume_calls = ready_calls = 0;
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    rp_w32(c, head, head);
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_head_prev), head);
    rp_w32(c, deadline, 1000); rp_core_set_downcount(c, 900);
    rp_cd_w16(c, RP_DMA_CHANNEL(c, 2, channel), 2);
    rp_w32(c, RP_DMA_CHANNEL(c, 2, transfer_callback), 0x12C74);
    rp_w32(c, callback, 0x8CAC);
    rp_w32(c, addr, 0x123457); rp_w32(c, block, 0x12340004);
    rp_w32(c, control, 0x01000401);
    rp_w32(c, RP_DMA_ADDRESS(c, priority), 0x800);
    resume_result = 0x801234AB;
    rp_pops_dma_resume(c, node);
    assert(resume_calls == 1 && resume_address == 0x123457);
    assert(resume_bytes == 0x12340004 && resume_control == 0x01000401);
    assert(rp_u32(c, addr) == 0x1234A8 && rp_u32(c, callback) == 0x8CAC);
    assert(rp_u32(c, RP_DMA_CHANNEL(c, 2, event.prev)) == head);
    assert(rp_u32(c, RP_DMA_CHANNEL(c, 2, event.deadline_cycles)) == 107);
    assert(rp_u32(c, control) == 0x01000401);

    rp_pops_remove_event(c, node);
    resume_result = 19;
    rp_pops_dma_resume(c, node);
    assert(rp_u32(c, addr) == 0 && rp_u32(c, callback) == 0x8B1C);
    assert(rp_u32(c, block) == 0x12340004 && rp_u32(c, control) == 0x01000401);
    assert(rp_u32(c, RP_DMA_CHANNEL(c, 2, event.deadline_cycles)) == 119);

    rp_pops_remove_event(c, node);
    rp_w32(c, callback, 0x8CAC); rp_w32(c, addr, 0x400);
    rp_w32(c, RP_DMA_CHANNEL(c, 2, completion_debit), 3);
    const uint32_t before = rp_core_downcount(c);
    resume_result = 0;
    rp_pops_dma_resume(c, node);
    assert(rp_u32(c, addr) == 0 && rp_u32(c, block) == 0);
    assert(rp_u32(c, control) == 0x401 && rp_core_downcount(c) == before - 3);
    assert(!rp_u32(c, RP_DMA_CHANNEL(c, 2, event.prev)));

    rp_w32(c, RP_DMA_ADDRESS(c, priority), 0);
    rp_w32(c, addr, 0x700); rp_w32(c, callback, 0x8CAC);
    const unsigned calls_before = resume_calls;
    rp_pops_dma_resume(c, node);
    assert(ready_calls == 1 && resume_calls == calls_before);
    assert(rp_u32(c, addr) == 0x700 && rp_u32(c, callback) == 0x8B1C);
    assert(!rp_u32(c, RP_DMA_CHANNEL(c, 2, event.prev)));
    rp_cd_w16(c, RP_DMA_CHANNEL(c, 1, channel), 1);
    rp_w32(c, RP_DMA_CHANNEL(c, 1, event.callback), 0x8CAC);
    rp_pops_dma_resume(c, RP_DMA_ADDRESS(c, channels[1]));
    assert(ready_calls == 1 && rp_u32(c, RP_DMA_CHANNEL(c, 1, event.callback)) == 0x8B1C);
    resume_fixture = false;
}

static void check_ordering_table_dma(rp_context *c)
{
    const uint32_t ram = RP_DMA_OTC_RAM_VIEW;
    c->regions[0] = (rp_region){ram, 0x24000, malloc(0x24000)};
    assert(c->regions[0].bytes);
    memset(c->regions[0].bytes, 0xA5, c->regions[0].size);
    assert(rp_pops_dma_clear_ordering_table(c, 0x100, 16, 0) == 1);
    assert(rp_pops_dma_clear_ordering_table(c, 0, 16, RP_DMA_OTC_CONTROL) == 1);
    assert(rp_u32(c, ram + 0x100) == 0xA5A5A5A5);
    assert(rp_pops_dma_clear_ordering_table(c, 0x100, 16, RP_DMA_OTC_CONTROL) == 16);
    assert(rp_u32(c, ram + 0xF0) == 0xA5A5A5A5);
    assert(rp_u32(c, ram + 0xF4) == RP_DMA_OTC_TERMINATOR);
    assert(rp_u32(c, ram + 0xF8) == 0xF4 && rp_u32(c, ram + 0xFC) == 0xF8);
    assert(rp_u32(c, ram + 0x100) == 0xFC && rp_u32(c, ram + 0x104) == 0xA5A5A5A5);
    assert(rp_pops_dma_clear_ordering_table(c, 8, 32, RP_DMA_OTC_CONTROL) == 8);
    assert(rp_u32(c, ram) == 0xA5A5A5A5);
    assert(rp_u32(c, ram + 4) == RP_DMA_OTC_TERMINATOR && rp_u32(c, ram + 8) == 4);
    assert(rp_pops_dma_clear_ordering_table(c, 0x23000, 0x20004, RP_DMA_OTC_CONTROL) == 0x20004);
    assert(rp_u32(c, ram + 0x3000) == RP_DMA_OTC_TERMINATOR);
    assert(rp_u32(c, ram + 0x13000) == 0x12FFC);
    assert(rp_u32(c, ram + 0x23000) == 0x22FFC);
    assert(rp_u32(c, (ram | 0x40000000) + 0x23000) == 0x22FFC);

    /* Exercise callback selection and actual completion, not a forced CHCR. */
    rp_w32(c, RP_DMA_CHANNEL(c, 6, event.prev), 0);
    rp_cd_w16(c, RP_DMA_CHANNEL(c, 6, channel), 6);
    rp_w32(c, RP_DMA_CHANNEL(c, 6, transfer_callback), 0x9364);
    rp_w32(c, RP_DMA_ADDRESS(c, priority), 0x08000000);
    rp_w32(c, RP_DMA_ADDRESS(c, interrupt_control), 0);
    rp_cd_w16(c, RP_DMA_ADDRESS(c, pending_channels), 0);
    rp_w32(c, RP_DMA_REGISTER(c, 6, address), 0x120);
    rp_w32(c, RP_DMA_REGISTER(c, 6, block_control), 4);
    rp_w32(c, RP_DMA_REGISTER(c, 6, channel_control), RP_DMA_OTC_CONTROL);
    rp_core_set_downcount(c, 100);
    rp_pops_dma_try_channel(c, 6);
    assert(rp_u32(c, ram + 0x114) == RP_DMA_OTC_TERMINATOR);
    assert(rp_u32(c, ram + 0x120) == 0x11C);
    assert(rp_u32(c, RP_DMA_REGISTER(c, 6, address)) == 0x110);
    assert(!(rp_u32(c, RP_DMA_REGISTER(c, 6, channel_control)) & 0x01000000));
    assert(!rp_u32(c, RP_DMA_REGISTER(c, 6, block_control)));
    assert(rp_core_downcount(c) == 89);
    free(c->regions[0].bytes);
    c->regions[0] = (rp_region){0};
}

static void check_serial_controller(rp_context *c)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    const uint32_t head = RP_CORE_CLOCK_ADDRESS(c, event_head_next);
    const uint32_t node = RP_SERIAL_PORT(c, 0, event);
    rp_w32(c, head, head);
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_head_prev), head);
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 1000);
    rp_core_set_downcount(c, 500);
    rp_cd_w16(c, RP_SERIAL_PORT(c, 0, status), 5);
    rp_cd_w16(c, RP_SERIAL_PORT(c, 0, bit_cycles), 4);
    rp_w32(c, RP_SERIAL_PORT(c, 0, transfer_callback), 0x9E64);
    rp_w8(c, RP_SERIAL_PORT(c, 0, device_kind), 1);
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, connected), 1);
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, id_byte), 0x41);
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, response_length), 2);
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, sync_byte), 0x5A);
    rp_w32(c, RP_CONTROLLER_PORT(c, 0, response_bytes), UINT32_MAX);

    const uint8_t tx[] = {1, 0x42, 0, 0, 0, 0};
    const uint8_t rx[] = {0xFF, 0x41, 0x5A, 0xFF, 0xFF, 0xFF};
    for (unsigned i = 0; i < sizeof(tx); ++i) {
        rp_pops_serial_data_write(c, 0x1F801040, tx[i]);
        assert(rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev)));
        rp_pops_remove_event(c, node);
        rp_pops_serial_event(c, node, 0x9E64);
        assert(rp_cd_u8(c, RP_SERIAL_PORT(c, 0, receive_data)) == rx[i]);
        if (i + 1 < sizeof(tx)) {
            assert(rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev)));
            rp_pops_remove_event(c, node);
            rp_pops_serial_event(c, node, 0xA220);
        }
    }
    assert(!rp_u32(c, RP_FIELD_ADDRESS(node, rp_guest_event_layout, prev)));
    assert(rp_u32(c, RP_SERIAL_PORT(c, 0, protocol_callback)) == 0x1A574);

    rp_cd_w16(c, RP_SERIAL_PORT(c, 0, status), 0x195);
    rp_pops_serial_control_write(c, 0x1F80104A, 0x40);
    assert(rp_cd_u16(c, RP_SERIAL_PORT(c, 0, status)) == 5);
    assert(!rp_cd_u16(c, RP_SERIAL_PORT(c, 0, control)));

    /* +0xA138 is a delay-slot store: reset clears either port's phase,
     * while only the primary port replaces its status with 5. */
    rp_w8(c, RP_SERIAL_PORT(c, 1, transfer_phase), 37);
    rp_cd_w16(c, RP_SERIAL_PORT(c, 1, status), 0x195);
    rp_pops_serial_control_write(c, 0x1F80105A, 0x40);
    assert(!rp_cd_u8(c, RP_SERIAL_PORT(c, 1, transfer_phase)));
    assert(rp_cd_u16(c, RP_SERIAL_PORT(c, 1, status)) == 0x195);
    rp_core_set_downcount(c, 100);
    rp_w32(c, RP_DMA_ADDRESS(c, deferred_frame_debit), (uint32_t)-4);
    rp_pops_serial_control_write(c, 0x1F80104A, 2);
    assert(rp_core_downcount(c) == 104);
    assert(!rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit)));

    /* +0x9FB0 uses the current downcount, not sample_cycles. */
    const uint32_t secondary = RP_SERIAL_PORT(c, 1, event);
    rp_w32(c, head, secondary); rp_w32(c, head + 4, secondary);
    rp_w32(c, secondary, head); rp_w32(c, secondary + 4, head);
    rp_w32(c, RP_SERIAL_PORT(c, 1, event.callback), 0x1A56C);
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 1000);
    rp_core_set_downcount(c, 500);
    rp_w32(c, RP_SERIAL_PORT(c, 1, previous_sample_cycles), 490);
    rp_w32(c, RP_SERIAL_PORT(c, 1, sample_cycles), 7);
    rp_w32(c, RP_DMA_ADDRESS(c, deferred_frame_debit), 600);
    assert(rp_pops_serial_read(c, 0x1F801054, 2) == 0x195);
    assert(rp_core_downcount(c) == 0);
    assert(rp_u32(c, RP_DMA_ADDRESS(c, deferred_frame_debit)) == 100);
    assert(rp_u32(c, RP_SERIAL_PORT(c, 1, previous_sample_cycles)) == 7);
    assert(rp_u32(c, RP_SERIAL_PORT(c, 1, sample_cycles)) == 500);
    assert(!rp_u32(c, secondary + 4));

    /* A nonzero extended-response flag selects six bytes, not flag bytes.
     * The terminal byte is still indexed by the configured response length. */
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, extended_response_active), 1);
    rp_w8(c, RP_CONTROLLER_PORT(c, 0, command), 0x42);
    rp_w32(c, RP_SERIAL_PORT(c, 0, protocol_callback), 0xA250);
    for (unsigned i = 0; i < 6; ++i)
        rp_w8(c, RP_CONTROLLER_PORT(c, 0, response_bytes[i]), (uint8_t)(0x30 + i));
    for (unsigned phase = 3; phase <= 9; ++phase) {
        rp_w8(c, RP_SERIAL_PORT(c, 0, transfer_phase), (uint8_t)phase);
        rp_pops_serial_event(c, node, 0x9E64);
        assert(rp_cd_u8(c, RP_SERIAL_PORT(c, 0, receive_data)) ==
               (phase < 9 ? 0x30 + phase - 3 : 0x32));
        if (phase < 9) rp_pops_remove_event(c, node);
    }
    assert(rp_u32(c, RP_SERIAL_PORT(c, 0, protocol_callback)) == 0x1A574);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c); c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    const uint32_t head = c->gp + 0x1B8, event = c->gp + 0x280;
    rp_w32(c, head, event); rp_w32(c, head + 4, event);
    rp_w32(c, event, head); rp_w32(c, event + 4, head);
    rp_w32(c, event + 8, 100); rp_w32(c, event + 12, 0x1265C);
    rp_w32(c, c->gp + 0x1AC, 100); rp_w32(c, c->gp + 0x1B0, (uint32_t)-5);
    rp_w32(c, c->gp + 0x1B4, 0x09B80040);
    assert(rp_pops_dispatch_events(c) == 0x09B80040);
    assert(callbacks == 1 && rp_u32(c, c->gp + 0x1AC) == 200);
    assert(rp_u32(c, c->gp + 0x1B0) == 92); /* 200 - (105 + 3). */
    assert(rp_pops_dispatch_events(c) == 0x09B80040 && callbacks == 1);
    assert(rp_u32(c, c->gp + 0x1B0) == 92);
    const uint32_t later = event + 0x20;
    rp_w32(c, event, later); rp_w32(c, later + 4, event);
    rp_w32(c, later, head); rp_w32(c, head + 4, later); rp_w32(c, later + 8, 300);
    rp_pops_remove_event(c, event);
    assert(rp_u32(c, head) == later && rp_u32(c, later + 4) == head);
    assert(!rp_u32(c, event + 4) && rp_u32(c, c->gp + 0x1B0) == 192);
    assert(rp_u32(c, c->gp + 0x1AC) == 300);
    rp_w32(c, c->gp + 0x2070, 5); rp_w32(c, c->gp + 0x2074, 0);
    rp_w32(c, c->gp + 0x130, 0x401);
    rp_pops_irq_write(c, 0x1074, 4);
    assert(rp_u32(c, c->gp + 0x134) == 0x400 && rp_u32(c, c->gp + 0x1B0) == 0);
    assert(rp_u32(c, c->gp + 0x1AC) == 108);
    rp_pops_irq_write(c, 0x1070, 1);
    assert(rp_u32(c, c->gp + 0x2070) == 1 && rp_u32(c, c->gp + 0x134) == 0);
    rp_w32(c, c->gp + 0x1AC, 200); rp_w32(c, c->gp + 0x1B0, 100);
    rp_w32(c, c->gp + 0x1C4, 100); rp_w32(c, c->gp + 0x1C8, 100);
    rp_w8(c, c->gp + 0x1C3, 4);
    assert(rp_pops_irq_read(c, 0x1F801074) == 4);
    assert(rp_u32(c, c->gp + 0x1B0) == 92);
    assert(*(uint8_t *)rp_memory(c, c->gp + 0x1C3, 1) == 8);
    assert(rp_pops_irq_read(c, 0x1F801074) == 4);
    assert(rp_u32(c, c->gp + 0x1B0) == 76);
    assert(*(uint8_t *)rp_memory(c, c->gp + 0x1C3, 1) == 16);
    rp_w32(c, c->gp + 0x20F0, 0x77777777);
    rp_pops_dma_control_write(c, 0x1F8010F0, 0x07654321, 2);
    assert(rp_u32(c, c->gp + 0x20F0) == 0x07654321);
    rp_pops_dma_control_write(c, 0x1F8010F0, 0x07654329, 2);
    assert(rp_u32(c, c->gp + 0x20F0) == 0x07654329); /* Channel 0 remains idle. */
    rp_w32(c, c->gp + 0x20F4, 0x01000000);
    rp_pops_dma_control_write(c, 0x1F8010F4, 0x00800000, 2);
    assert(rp_u32(c, c->gp + 0x20F4) == 0x81800000);
    rp_pops_dma_control_write(c, 0x1F8010F4, 0x01800000, 2);
    assert(rp_u32(c, c->gp + 0x20F4) == 0x00800000);
    const uint32_t timer = RP_TIMER_BASE(c, 1);
    rp_w32(c, RP_TIMER_FIELD(timer, target_with_flags), 0x10000);
    rp_pops_timer_write(c, 0x1F801114, 0x100);
    assert((rp_u32(c, RP_TIMER_FIELD(timer, mode_with_status)) & 0x3FF) == 0x100);
    assert(rp_cd_u8(c, RP_TIMER_FIELD(timer, clock_shift)) == 11);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, origin_cycles)) == 124 && !rp_u32(c, RP_TIMER_FIELD(timer, event.prev)));
    rp_pops_timer_write(c, 0x1F801114, 0x18);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, event.prev)));
    rp_pops_timer_write(c, 0x1F801118, 10);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, target_with_flags)) == 10);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, event.deadline_cycles)) == 134 && rp_u32(c, head) == timer);
    assert(rp_u32(c, c->gp + 0x1B0) == 10);

    /* Timer read shares the writer's state and clears only mode/status bits,
     * with signed-halfword conversion confined to counter reads. */
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 0x9003);
    rp_core_set_downcount(c, 0);
    rp_w32(c, RP_TIMER_FIELD(timer, target_with_flags), 0x10000);
    rp_w32(c, RP_TIMER_FIELD(timer, origin_cycles), 0);
    rp_w32(c, RP_TIMER_FIELD(timer, mode_with_status), 0);
    rp_w8(c, RP_TIMER_FIELD(timer, clock_shift), 0);
    assert(rp_pops_timer_read(c, 0x1F801110, 2) == 0x9003);
    assert(rp_pops_timer_read(c, 0x1F801110, 1) == 0xFFFF9003);
    assert(rp_pops_timer_read(c, 0x1F801110, 5) == 0x9003);
    rp_w32(c, RP_TIMER_FIELD(timer, mode_with_status), 0x1C18);
    assert(rp_pops_timer_read(c, 0x1F801114, 2) == 0x1C18);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, mode_with_status)) == 0x18);

    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 30);
    rp_core_set_downcount(c, 3);
    rp_w32(c, RP_TIMER_FIELD(timer, target_with_flags), 10);
    rp_w32(c, RP_TIMER_FIELD(timer, origin_cycles), 0);
    rp_w32(c, RP_TIMER_FIELD(timer, mode_with_status), 8);
    assert(rp_pops_timer_read(c, 0x1F801110, 2) == 7);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, origin_cycles)) == 20);
    assert(rp_pops_timer_read(c, 0x1F801114, 2) == 0x808);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, mode_with_status)) == 8);
    assert(rp_core_downcount(c) == 3);

    rp_w32(c, RP_TIMER_FIELD(timer, target_with_flags), 0x8000000A);
    rp_w32(c, RP_TIMER_FIELD(timer, origin_cycles), 0x1234FFFF);
    assert(rp_pops_timer_read(c, 0x1F801110, 2) == 0x1234FFFF);
    assert(rp_pops_timer_read(c, 0x1F801110, 1) == UINT32_MAX);
    rp_w32(c, RP_TIMER_FIELD(timer, mode_with_status), 0xFFFF8821);
    assert(rp_pops_timer_read(c, 0x1F801114, 1) == 0xFFFF8821);
    assert(rp_u32(c, RP_TIMER_FIELD(timer, mode_with_status)) == 0x21);

    const uint32_t timer2 = RP_TIMER_BASE(c, 2);
    rp_w32(c, RP_TIMER_FIELD(timer2, target_with_flags), 0x10000);
    rp_w32(c, RP_TIMER_FIELD(timer2, origin_cycles), 3);
    rp_w8(c, RP_TIMER_FIELD(timer2, clock_shift), 3);
    assert(rp_pops_timer_read(c, 0x1F801120, 2) == 3);

    /* DMA readers must preserve the busy bit, apply the original width table
     * and refund four cycles rather than manufacture a completed transfer. */
    const uint32_t chcr = RP_DMA_REGISTER(c, 2, channel_control);
    rp_w32(c, chcr, UINT32_C(0x9182FEDC));
    const uint32_t expected[] = {
        UINT32_C(0xFFFFFFDC), UINT32_C(0xFFFFFEDC), UINT32_C(0x9182FEDC),
        0xFEDC, 0xDC, 0xFEDC, 0xFEDC
    };
    for (unsigned width = 0; width < 7; ++width) {
        rp_core_set_downcount(c, UINT32_MAX - 5);
        assert(rp_pops_dma_read(c, 0x1F8010A8, width) == expected[width]);
        assert(rp_core_downcount(c) == UINT32_MAX - 1);
        assert(rp_u32(c, chcr) == UINT32_C(0x9182FEDC));
    }
    assert(rp_pops_dma_read(c, 0x1F8010A9, 4) == 0xFE);
    assert(rp_pops_dma_read(c, 0x1F8010AA, 5) == 0x9182);
    check_ordering_table_dma(c);
    check_dma_resume(c);
    check_serial_controller(c);
    fclose(c->trace); free(c);
    puts("Events/timers/DMA/serial: controller poll, reads, OTC, resume and completion passed.");
    return 0;
}
