#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>

static unsigned schedules, removals;
void rp_pops_schedule_event(rp_context *c, uint32_t event, uint32_t delay)
{
    assert(event == c->gp + 0x350 && delay == 0x869);
    ++schedules; rp_w32(c, event + 4, c->gp + 0x1B8);
}
void rp_pops_remove_event(rp_context *c, uint32_t event)
{
    assert(event == c->gp + 0x350); ++removals; rp_w32(c, event + 4, 0);
}
void rp_pops_me_poll(rp_context *c) { (void)c; abort(); }
static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c)); assert(c);
    c->trace = tmpfile(); assert(c->trace); c->gp = 0x10000;
    c->regions[2] = (rp_region){0x09F40000, 0xC0000, calloc(1, 0xC0000)};
    assert(c->regions[2].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    rp_w32(c, c->gp + 0x1AC, 100000); rp_w32(c, c->gp + 0x1B0, 100000);
    rp_pops_spu_write_register(c, 0x1F801D80, 0xABCD, 1);
    assert(half(c, 0x49F40180) == 0xABCD);
    rp_pops_spu_write_register(c, 0x1F801D81, 0x34, 0);
    assert(half(c, 0x49F40180) == 0x3400);
    rp_pops_spu_write_register(c, 0x1D84, 0x11223344, 2);
    assert(rp_u32(c, 0x49F40184) == 0x11223344 && rp_u32(c, c->gp + 0x1B0) == 99996);
    rp_pops_spu_write_register(c, 0x1DAE, 0xFFFF, 1);
    assert(!half(c, 0x49F401AE) && rp_u32(c, c->gp + 0x1B0) == 99995);
    rp_pops_spu_write_register(c, 0x1C06, 0x100, 1);
    assert(half(c, 0x49F40006) == 0x202 && rp_u32(c, 0x49F40288) == 1);
    assert(!*(uint8_t *)rp_memory(c, 0x49F4029D, 1));
    rp_pops_spu_write_register(c, 0x1D8A, 0x80, 1);
    assert(rp_u32(c, 0x49F40280) == 0x800000);
    rp_pops_spu_write_register(c, 0x1D8E, 0x80, 1);
    assert(rp_u32(c, 0x49F40284) == 0x800000);
    rp_pops_spu_write_register(c, 0x1DA6, 2, 1);
    rp_pops_spu_write_register(c, 0x1DA8, 0xBEEF, 1);
    rp_pops_spu_write_register(c, 0x1DAA, 0x10, 1);
    assert(half(c, 0x49F402D0) == 0xBEEF && rp_u32(c, c->gp + 0x34C) == 9);
    assert(!*(uint8_t *)rp_memory(c, c->gp + 0x34B, 1));
    rp_pops_spu_write_register(c, 0x1DAA, 0x40, 1);
    rp_pops_spu_write_register(c, 0x1DAA, 0, 1);
    assert(schedules == 1 && removals == 1);
    rp_w32(c, c->gp + 0x1B0, 1000);
    rp_w32(c, 0x49F40180, 0xABCDFF80);
    assert(rp_pops_spu_read_register(c, 0x1F801D80, 0) == 0xFFFFFF80);
    assert(rp_pops_spu_read_register(c, 0x1F801D80, 4) == 0x80);
    assert(rp_pops_spu_read_register(c, 0x1F801D80, 1) == 0xFFFFFF80);
    assert(rp_pops_spu_read_register(c, 0x1F801D80, 5) == 0xFF80);
    assert(rp_pops_spu_read_register(c, 0x1F801D80, 2) == 0xABCDFF80);
    assert(rp_u32(c, c->gp + 0x1B0) == 943);
    rp_w8(c, 0x49F401AE, 0x20); rp_w8(c, c->gp + 0x34A, 0x40);
    assert(rp_pops_spu_read_register(c, 0x1F801DAE, 5) == 0xE0);
    assert(rp_pops_spu_read_register(c, 0x1F801DAE, 1) == 0xE0);
    assert(half(c, 0x49F401AE) == 0x20);
    assert(*(uint8_t *)rp_memory(c, c->gp + 0x34A, 1) == 0x40);
    fclose(c->trace); free(c->regions[2].bytes); free(c);
    puts("SPU registers: widths, cycle debits, status latch, voice/key masks and FIFO passed.");
    return 0;
}
