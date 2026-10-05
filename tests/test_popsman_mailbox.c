#include "../src/popsman_mailbox.h"
#include <assert.h>
#include <stdio.h>

typedef struct trace {
    unsigned count;
    unsigned event[4];
    uint32_t addr, value;
} trace;

static void record_write(void *ctx, uint32_t addr, uint32_t value)
{
    trace *t = ctx;
    assert(t->count < 4);
    t->event[t->count++] = 1;
    t->addr = addr;
    t->value = value;
}

static void record_sync(void *ctx)
{
    trace *t = ctx;
    assert(t->count < 4);
    t->event[t->count++] = 2;
}

int main(void)
{
    const uint32_t volume[][2] = {
        {0, 0}, {31, 0}, {32, 1}, {33, 1},
        {0x7fffffff, 0x03ffffff}, {0x80000000, 0x04000000},
        {0xfffffffe, 0x07ffffff}, {0xffffffff, 0x07ffffff}
    };
    const uint32_t notify[][4] = {
        {0, 0, 0, 2}, {1, 0, 0, 3},
        {0xffffffff, 0, 0, 1}, {0xfffffffe, 0, 0, 0},
        {0x80000000, 0x00100000, 0x80000023, 0},
        {0x7fffffff, 0x00100000, 0, 0x80000001},
        {0xffffffff, 0x000fffff, 0, 1},
        {0xffffffff, 0xffffffff, 0x80000023, 0}
    };
    const uint32_t ge[][2] = {
        {0, 0}, {1, 1}, {0x1fffffff, 0x1fffffff}, {0x20000000, 0},
        {0x7fffffff, 0x1fffffff}, {0x80000000, 0},
        {0xbd40010c, 0x1d40010c}, {0xffffffff, 0x1fffffff}
    };
    trace t;
    const repops_pm_bus bus = {&t, record_write, record_sync};
    for (unsigned i = 0; i < sizeof volume / sizeof *volume; ++i) {
        t = (trace){0};
        assert(repops_pm_c93c56f8(&bus, volume[i][0]) == 0);
        assert(t.count == 2 && t.event[0] == 1 && t.event[1] == 2);
        assert(t.addr == 0xbfc007f4 && t.value == volume[i][1]);
    }
    for (unsigned i = 0; i < sizeof notify / sizeof *notify; ++i) {
        t = (trace){0};
        assert(repops_pm_0babd960(&bus, notify[i][0], notify[i][1]) == notify[i][2]);
        if (notify[i][2] != 0) {
            assert(t.count == 0);
        } else {
            assert(t.count == 1 && t.event[0] == 1);
            assert(t.addr == 0xbfc007f8 && t.value == notify[i][3]);
        }
    }
    for (unsigned i = 0; i < sizeof ge / sizeof *ge; ++i) {
        t = (trace){0};
        repops_pm_e7f06e2b(&bus, ge[i][0]);
        assert(t.count == 1 && t.event[0] == 1);
        assert(t.addr == 0xbd40010c && t.value == ge[i][1]);
    }
    puts("24 mailbox/GE callback-contract cases passed");
    return 0;
}
