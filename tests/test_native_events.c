#include "../src/native/runtime.h"
#include "../src/native/pops_timer.h"
#include "../src/native/pops_dma.h"
#include <assert.h>
#include <stdlib.h>

static unsigned callbacks;
uint32_t rp_pops_gpu_dma_transfer(rp_context *c, uint32_t a, uint32_t n, uint32_t f)
{ (void)c; (void)a; (void)n; (void)f; abort(); }
uint32_t rp_pops_cd_dma_transfer(rp_context *c, uint32_t a, uint32_t n, uint32_t f)
{ (void)c; (void)a; (void)n; (void)f; abort(); }
void rp_pops_cd_event(rp_context *c, uint32_t event, uint32_t callback)
{ (void)c; (void)event; (void)callback; abort(); }

/* One scripted callback isolates the scheduler's unlink and time accounting. */
void rp_pops_graphics_event(rp_context *c, uint32_t callback)
{
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
    fclose(c->trace); free(c);
    puts("Guest events/timers/DMA: typed register reads, widths, clocks and status effects passed.");
    return 0;
}
