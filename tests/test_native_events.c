#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>

static unsigned callbacks;

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
    const uint32_t timer = c->gp + 0x64C + 0x20;
    rp_w32(c, timer + 0x10, 0x10000);
    rp_pops_timer_write(c, 0x1F801114, 0x100);
    assert((rp_u32(c, timer + 0x18) & 0x3FF) == 0x100);
    assert(*(uint8_t *)rp_memory(c, timer + 0x1D, 1) == 11);
    assert(rp_u32(c, timer + 0x14) == 124 && !rp_u32(c, timer + 4));
    rp_pops_timer_write(c, 0x1F801114, 0x18);
    assert(rp_u32(c, timer + 4));
    rp_pops_timer_write(c, 0x1F801118, 10);
    assert(rp_u32(c, timer + 0x10) == 10);
    assert(rp_u32(c, timer + 8) == 134 && rp_u32(c, head) == timer);
    assert(rp_u32(c, c->gp + 0x1B0) == 10);
    fclose(c->trace); free(c);
    puts("Guest scheduler: unlink, callback debit, overshoot and future-event wait passed.");
    return 0;
}
