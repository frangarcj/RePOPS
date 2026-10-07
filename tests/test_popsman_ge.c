#include "../src/popsman_ge.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { SUSPEND, WRITE, EPC, CACHE, READ, RESUME, ENQUEUE, SYNC };
typedef struct { unsigned kind; uint32_t a, b; } event;
typedef struct {
    event events[64]; unsigned count, polls, ready_poll;
    uint32_t epc, result, start;
} fixture;
static void add(fixture *f, unsigned kind, uint32_t a, uint32_t b)
{
    assert(f->count < sizeof(f->events) / sizeof(f->events[0]));
    f->events[f->count++] = (event){kind, a, b};
}
static uint32_t suspend(void *p)
{ add(p, SUSPEND, 0, 0); return 0xA55A1234; }
static uint32_t read_epc(void *p)
{ fixture *f = p; add(f, EPC, 0, 0); return f->epc; }
static void cache(void *p, uint32_t op, uint32_t address)
{ add(p, CACHE, op, address); }
static uint32_t read32(void *p, uint32_t address)
{
    fixture *f = p;
    add(f, READ, address, 0);
    assert(address == REPOPS_PM_GE_COMPLETION_STATUS);
    ++f->polls;
    return f->polls == f->ready_poll ? 0x84 : 0x80;
}
static void write32(void *p, uint32_t address, uint32_t value)
{ add(p, WRITE, address, value); }
static void resume(void *p, uint32_t saved)
{ assert(saved == 0xA55A1234); add(p, RESUME, saved, 0); }
static uint32_t enqueue(void *p, uint32_t start, uint32_t stall, int32_t cb, uint32_t args)
{
    fixture *f = p;
    assert(start == stall && cb == -1 && args == 0);
    f->start = start;
    add(f, ENQUEUE, start, stall);
    return f->result;
}
static uint32_t sync_list(void *p, uint32_t id, uint32_t mode)
{
    fixture *f = p;
    assert(f->events[f->count - 1].kind == ENQUEUE);
    add(f, SYNC, id, mode);
    return 0x80000001; /* The original does not replace the enqueue result. */
}
static void expect(fixture *f, unsigned *index, unsigned kind, uint32_t a, uint32_t b)
{
    assert(*index < f->count);
    const event actual = f->events[(*index)++];
    assert(actual.kind == kind && actual.a == a && actual.b == b);
}
int main(void)
{
    const unsigned ready[] = {1, 7, 20, 0, 21};
    const uint32_t addresses[] = {0x49A00160, 0, 0xFFFFFFFF, 0xE9A00160};
    for (unsigned r = 0; r < 5; ++r) for (unsigned a = 0; a < 4; ++a) {
        fixture f = {.ready_poll = ready[r], .epc = 0xFFFFFFC0,
                     .result = a & 1 ? 0x80000023 : 186};
        const repops_pm_ge_bus bus = {&f, suspend, read_epc, cache, read32,
                                       write32, resume, enqueue, sync_list};
        const uint32_t result = repops_pm_7014c540(&bus, 185, addresses[a]);
        const uint32_t physical = addresses[a] & 0x1FFFFFFF;
        const uint32_t uncached = physical | 0x40000000;
        const unsigned polls = ready[r] && ready[r] <= 20 ? ready[r] : 20;
        unsigned index = 0;
        expect(&f, &index, SUSPEND, 0, 0);
        expect(&f, &index, WRITE, uncached - 4, 0x0F000000);
        expect(&f, &index, WRITE, REPOPS_PM_GE_STALL, physical);
        expect(&f, &index, EPC, 0, 0);
        for (unsigned i = 0; i < polls; ++i) {
            expect(&f, &index, CACHE, 0xA, f.epc + (i + 1) * 64);
            expect(&f, &index, READ, REPOPS_PM_GE_COMPLETION_STATUS, 0);
        }
        assert(f.polls == polls);
        if (ready[r] && ready[r] <= 20) {
            expect(&f, &index, WRITE, REPOPS_PM_GE_ACK, 4);
            expect(&f, &index, RESUME, 0xA55A1234, 0);
            assert(result == 185);
        } else {
            expect(&f, &index, WRITE, uncached, 0x0C000000);
            expect(&f, &index, WRITE, REPOPS_PM_GE_STALL, 0);
            expect(&f, &index, RESUME, 0xA55A1234, 0);
            expect(&f, &index, ENQUEUE, uncached, uncached);
            expect(&f, &index, SYNC, 185, 0);
            assert(result == f.result);
        }
        assert(index == f.count);
    }
    puts("POPSMAN GE: 20 ordered contracts cover early/last/no completion, masks, cache wrap, fallback and return bits; no device timing.");
    return 0;
}
