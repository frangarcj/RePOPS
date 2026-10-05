#include "../src/native/me_worker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define OUT UINT32_C(0xBE000000)
#define REQ UINT32_C(0xBFC007F8)
#define ACK UINT32_C(0xBFC007F0)

typedef struct event { unsigned kind; uint32_t address, value; } event;
typedef struct fixture {
    uint32_t entry, control, status, busy, config_busy, sample_value, ack;
    uint16_t indirect;
    bool sample_ready;
    unsigned count, sample_calls, writes, sample_writes, acknowledgements, flushes;
    uint32_t last_output, indirect_address;
    event events[2048];
} fixture;

static void log_event(fixture *f, unsigned kind, uint32_t a, uint32_t v)
{
    assert(f->count < sizeof(f->events) / sizeof(f->events[0]));
    f->events[f->count++] = (event){kind, a, v};
}
static uint32_t provider(void *ctx, uint32_t offset)
{
    fixture *f = ctx;
    assert(offset == 0x4C5C);
    log_event(f, 0, offset, f->entry);
    return f->entry;
}
static uint32_t read32(void *ctx, uint32_t address)
{
    fixture *f = ctx;
    uint32_t result;
    switch (address) {
    case 0xBC000044: result = 0xFFFFFFFF; break;
    case 0xBC00000C: result = 0x12345678; break; /* deliberate readback behavior */
    case OUT + 0xC: result = f->busy; break;
    case OUT + 0x50: result = f->config_busy; break;
    case OUT + 0x28: result = f->status; break;
    case REQ: result = f->control; break;
    case 0xBFC007F4: result = 0x345; break;
    default: assert(!"Unexpected read32"); result = 0;
    }
    log_event(f, 1, address, result);
    return result;
}
static uint16_t read16(void *ctx, uint32_t address)
{
    fixture *f = ctx;
    assert((address & 1) == 0);
    f->indirect_address = address;
    log_event(f, 2, address, f->indirect);
    return f->indirect;
}
static void write32(void *ctx, uint32_t address, uint32_t value)
{
    fixture *f = ctx;
    ++f->writes;
    if (address == OUT + 0x70) { ++f->sample_writes; f->last_output = value; }
    if (address == ACK) { ++f->acknowledgements; f->ack = value; }
    log_event(f, 3, address, value);
}
static void service(void *ctx, rp_me_service svc, uint32_t argument)
{
    fixture *f = ctx;
    if (svc == RP_ME_DDR_FLUSH) { assert(argument == 8); ++f->flushes; }
    log_event(f, 4, (uint32_t)svc, argument);
}
static bool sample(void *ctx, uint32_t entry, uint32_t *result)
{
    fixture *f = ctx;
    assert(entry == f->entry);
    ++f->sample_calls;
    log_event(f, 5, entry, f->sample_value);
    if (!f->sample_ready) return false;
    *result = f->sample_value;
    return true;
}
static rp_me_bus bus(fixture *f)
{
    return (rp_me_bus){f, provider, read32, read16, write32, service, sample};
}
static void advance_to_pump(rp_me_worker *w, rp_me_bus *b)
{
    for (unsigned step = 0; step < 64 && w->phase != RP_ME_PUMP; ++step)
        assert(rp_me_worker_step(w, b) == RP_ME_PROGRESS);
    assert(w->phase == RP_ME_PUMP);
}
static void callback_cycle(rp_me_worker *w, rp_me_bus *b)
{
    assert(w->phase == RP_ME_PUMP);
    assert(rp_me_worker_step(w, b) == RP_ME_PROGRESS);
    assert(w->phase == RP_ME_EMIT_PREVIOUS);
    assert(rp_me_worker_step(w, b) == RP_ME_PROGRESS);
    assert(w->phase == RP_ME_CALL_SAMPLE);
    assert(rp_me_worker_step(w, b) == RP_ME_CALLBACK_RETURNED);
}

int main(void)
{
    fixture f = {0};
    rp_me_bus b = bus(&f);
    rp_me_worker w;
    rp_me_worker_init(&w);
    assert(rp_me_worker_step(&w, NULL) == RP_ME_INVALID_HOST);
    assert(rp_me_worker_step(&w, &b) == RP_ME_PARKED);
    assert(f.events[8].address == 0xBC000008 && f.events[8].value == 0x12345678);
    unsigned before = f.count;
    assert(rp_me_worker_step(&w, &b) == RP_ME_PARKED && f.count == before);
    assert(!f.acknowledgements && !f.sample_calls);

    f = (fixture){.entry = 0x08804000, .busy = 7, .sample_ready = true};
    rp_me_worker_init(&w);
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    assert(rp_me_worker_step(&w, &b) == RP_ME_WAITING_IO);
    f.busy = 0; f.config_busy = 0x10000;
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    assert(rp_me_worker_step(&w, &b) == RP_ME_WAITING_IO);
    assert(f.events[f.count-1].address == 0xBFC00650);
    f.config_busy = 0;
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    assert(rp_me_worker_step(&w, &b) == RP_ME_WAITING_IO);
    assert(f.sample_writes == 0 && f.acknowledgements == 0);
    f.status = 0x20;
    advance_to_pump(&w, &b);
    assert(f.sample_writes == 24 && f.last_output == 0);

    f.sample_value = 0x80007FFF;
    callback_cycle(&w, &b);
    assert(f.last_output == 0 && w.packed == f.sample_value);
    assert(w.phase == RP_ME_PUMP && !f.acknowledgements);
    f.sample_value = 0x00010002;
    callback_cycle(&w, &b);
    assert(f.last_output == 0x80007FFF && w.packed == 0x00010002);
    f.sample_ready = false;
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    assert(rp_me_worker_step(&w, &b) == RP_ME_PROGRESS);
    before = f.writes;
    assert(rp_me_worker_step(&w, &b) == RP_ME_CALLBACK_UNAVAILABLE);
    assert(rp_me_worker_step(&w, &b) == RP_ME_CALLBACK_UNAVAILABLE);
    assert(f.writes == before && !f.acknowledgements);
    f.sample_ready = true; f.control = 0x4002; f.indirect = 0xFEDC;
    assert(rp_me_worker_step(&w, &b) == RP_ME_CALLBACK_RETURNED);
    assert(f.indirect_address == 0x4000 && w.packed == 0xFEDCFEDC);
    assert(!f.acknowledgements && w.phase == RP_ME_PUMP);

    f.control = 1; f.sample_value = 0x80007FFF;
    callback_cycle(&w, &b);
    assert(w.phase == RP_ME_DRAIN && !f.acknowledgements);
    f.status = 0;
    assert(rp_me_worker_step(&w, &b) == RP_ME_WAITING_IO);
    assert(!f.acknowledgements);
    f.status = 0x20;
    assert(rp_me_worker_step(&w, &b) == RP_ME_ACK_WRITTEN);
    assert(f.ack == 1 && f.last_output == 0xA0006000);
    assert(w.low_sample == 24576 && w.high_sample == -24576);
    f.status = 2; f.control = 0;
    before = f.sample_writes;
    assert(rp_me_worker_step(&w, &b) == RP_ME_ACK_WRITTEN);
    assert(f.sample_writes == before && f.ack == 0 && w.phase == RP_ME_RESET_OUTPUT);

    f.status = 0x20;
    advance_to_pump(&w, &b);
    f.control = 1;
    callback_cycle(&w, &b);
    f.control = 4; f.status = 2;
    assert(rp_me_worker_step(&w, &b) == RP_ME_ACK_WRITTEN);
    assert(f.ack == 4 && f.flushes == 1 && w.phase == RP_ME_STOPPED);
    before = f.count;
    assert(rp_me_worker_step(&w, &b) == RP_ME_PARKED && f.count == before);
    puts("Native ME worker: scripted bus/sample/order/pending/fade/control tests passed; not hardware or mixer validation.");
    return 0;
}
