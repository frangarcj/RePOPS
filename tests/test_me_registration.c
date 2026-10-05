#include "../src/me_registration.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef struct event {
    unsigned kind;
    uint32_t address;
    uint32_t value;
} event;

typedef struct fixture {
    uint32_t hi, lo, entry;
    event events[12];
    size_t count;
} fixture;

static void record(fixture *f, unsigned kind, uint32_t address, uint32_t value)
{
    assert(f->count < sizeof(f->events) / sizeof(f->events[0]));
    f->events[f->count++] = (event){kind, address, value};
}

static uint32_t read_word(void *context, uint32_t address)
{
    fixture *f = context;
    assert(address == REPOPS_ME_STACK_HI_WORD || address == REPOPS_ME_STACK_LO_WORD);
    const uint32_t value = address == REPOPS_ME_STACK_HI_WORD ? f->hi : f->lo;
    record(f, 0, address, value);
    return value;
}

static void write_word(void *context, uint32_t address, uint32_t value)
{
    fixture *f = context;
    if (address == REPOPS_ME_STACK_HI_WORD) f->hi = value;
    else if (address == REPOPS_ME_STACK_LO_WORD) f->lo = value;
    else {
        assert(address == REPOPS_ME_CALLBACK_SLOT);
        f->entry = value;
    }
    record(f, 1, address, value);
}

static void start_boundary(void *context, uint32_t k1)
{
    record(context, 2, 0x35D8, k1);
}

static unsigned cases;

static void run_case(uint32_t entry, uint32_t stack, uint32_t k1,
                     uint32_t initial_hi, uint32_t initial_lo, int rejected)
{
    fixture f = {.hi = initial_hi, .lo = initial_lo, .entry = 0xABCD1234};
    const repops_me_registration_host host = {&f, read_word, write_word, start_boundary};
    const uint32_t result = repops_me_register(&host, entry, stack, k1);
    if (rejected) {
        assert(result == UINT32_C(0x80000023));
        assert(f.count == 0);
        assert(f.hi == initial_hi && f.lo == initial_lo && f.entry == 0xABCD1234);
    } else {
        assert(result == 0 && f.count == 6);
        assert(f.entry == entry);
        assert(f.hi == (initial_hi | (stack >> 16)));
        assert(f.lo == (initial_lo | (stack & 0xFFFF)));
        assert(f.events[0].kind == 0 && f.events[0].address == 0x2F2C);
        assert(f.events[1].kind == 0 && f.events[1].address == 0x2F30);
        assert(f.events[2].kind == 1 && f.events[2].address == 0x4C5C);
        assert(f.events[3].kind == 1 && f.events[3].address == 0x2F30);
        assert(f.events[4].kind == 1 && f.events[4].address == 0x2F2C);
        assert(f.events[5].kind == 2 && f.events[5].value == (k1 << 11));
    }
    ++cases;
}

int main(void)
{
    const uint32_t values[] = {0, 1, 0x09FF8000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    const uint32_t k1s[] = {0, 1, 0x80000000};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (size_t j = 0; j < sizeof(k1s) / sizeof(k1s[0]); ++j) {
            /* None of these K1 values has bit 20 set. */
            run_case(values[i], values[i], k1s[j], 0x3C1D0000, 0x37BD0000, 0);
        }
    }
    run_case(0, 0, 0x100000, 0x3C1D0000, 0x37BD0000, 0);
    run_case(0x80000000, 0, 0x100000, 0x3C1D0000, 0x37BD0000, 1);
    run_case(0, 0x80000000, 0x100000, 0x3C1D0000, 0x37BD0000, 1);
    run_case(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x3C1D0000, 0x37BD0000, 1);
    run_case(1, 3, 0xFFFFFFFF, 0xABCD1234, 0x9876FEDC, 0);

    fixture f = {.hi = 0x3C1D0000, .lo = 0x37BD0000};
    const repops_me_registration_host host = {&f, read_word, write_word, start_boundary};
    assert(repops_me_register(&host, 0x100, 0x09FF8000, 0) == 0);
    assert(repops_me_register(&host, 0x200, 0x00010001, 0) == 0);
    assert(f.entry == 0x200 && f.hi == 0x3C1D09FF && f.lo == 0x37BD8001);
    ++cases;
    printf("ME registration: %u callback-contract cases passed (not hardware tests).\n", cases);
    return 0;
}
