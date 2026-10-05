#include "../src/me_startup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct event { uint32_t kind, address, value; } event;
typedef struct fixture {
    uint32_t config, mode, clock, old_ack, ack, control, callback;
    uint32_t target, after, delays, polls, token;
    uint32_t template_words[24], copied_words[24];
    event events[512];
    size_t count;
    int no_ack;
} fixture;

static void record(fixture *f, uint32_t k, uint32_t a, uint32_t v)
{
    assert(f->count < 512);
    f->events[f->count++] = (event){k, a, v};
}

static uint32_t rd(void *p, uint32_t a)
{
    fixture *f = p;
    uint32_t v = 0;
    switch (a) {
    case 0xBC100070: v = f->clock; break;
    case 0x4A08: v = f->config; break;
    case 0x4C68: v = f->mode; break;
    case 0x4C5C: v = f->callback; break;
    case 0xBFC00040: v = f->copied_words[0]; break;
    case 0xBFC007F0: v = f->ack; ++f->polls; break;
    default: assert(0);
    }
    record(f, 1, a, v);
    return v;
}

static void wr(void *p, uint32_t a, uint32_t v)
{
    fixture *f = p;
    record(f, 2, a, v);
    switch (a) {
    case 0xBC100070: f->clock = v; break;
    case 0xBFC00040: f->copied_words[0] = v; break;
    case 0xBFC007F0: f->ack = v; break;
    case 0xBFC007F8: f->control = v; break;
    default: assert(0);
    }
}

static void update_ack(fixture *f)
{
    /* Synthetic device response, not a hardware claim. */
    f->ack = !f->no_ack && f->delays >= f->after ? f->target : 3;
}

static uint32_t svc(void *p, uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2)
{
    fixture *f = p;
    record(f, 3, id, a0);
    switch (id) {
    case REPOPS_ME_SUSPEND_INTR: return f->token;
    case REPOPS_ME_RESUME_INTR: assert(a0 == f->token); break;
    case REPOPS_ME_COPY:
        assert(a0 == 0xBFC00040 && a1 == 0x2F28 && a2 == 0x60);
        memcpy(f->copied_words, f->template_words, sizeof(f->copied_words));
        break;
    case REPOPS_ME_DDR_FLUSH: assert(a0 == 4); break;
    case REPOPS_ME_RESET_RELEASE:
        assert(f->ack == 0);
        update_ack(f);
        break;
    case REPOPS_ME_CODEC_376399B6:
        assert(a0 == (f->target ^ 1));
        update_ack(f);
        break;
    case REPOPS_ME_DELAY:
        assert(a0 == 100);
        ++f->delays;
        update_ack(f);
        break;
    default: break;
    }
    return 0xDEADBEEF;
}

static fixture initial(uint32_t delay)
{
    fixture f = {0};
    f.after = delay;
    f.clock = 0xFFFFFFFF;
    f.token = 0xCAFE1234;
    f.callback = 0x8804000;
    for (unsigned i = 0; i < 24; ++i) f.template_words[i] = 0x12340000 + i * 4;
    return f;
}

int main(void)
{
    unsigned count = 0;
    const uint32_t modes[] = {0, 2, 3, 4};
    const uint32_t delays[] = {0, 1, 4};
    for (unsigned config = 0; config < 4; ++config)
    for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned d = 0; d < 3; ++d) {
        fixture f = initial(delays[d]);
        f.config = config | 0xDEAD0000;
        f.mode = modes[mode];
        f.target = 1;
        repops_me_startup_host h = {&f, rd, wr, svc};
        repops_me_wait w;
        repops_me_boot_begin(&h, &w);
        assert(f.polls == 0 && f.delays == 0);
        assert(f.clock == 0xFFFFFFFB);
        assert(f.control == ((f.mode & 2) ? f.mode : 1));
        assert(f.copied_words[0] == (f.template_words[0] | config));
        assert(memcmp(f.copied_words + 1, f.template_words + 1, 92) == 0);
        for (uint32_t i = 0; i < delays[d]; ++i) assert(!repops_me_wait_step(&h, &w));
        assert(repops_me_wait_step(&h, &w));
        assert(f.delays == delays[d] && f.polls == delays[d] + 1);
        size_t before = f.count;
        assert(repops_me_wait_step(&h, &w) && f.count == before);
        assert(f.events[0].address == REPOPS_ME_AVC_RESET);
        assert(f.events[1].address == REPOPS_ME_RESET_ENABLE);
        assert(f.events[2].address == REPOPS_ME_BUS_CLOCK);
        ++count;
    }
    const uint32_t arguments[] = {0, 1, 0xFFFFFFFF};
    for (unsigned a = 0; a < 3; ++a)
    for (unsigned cb = 0; cb < 2; ++cb)
    for (unsigned old = 0; old < 2; ++old)
    for (unsigned d = 0; d < 2; ++d) {
        fixture f = initial(d * 2);
        f.ack = f.old_ack = old ? 3 : 0;
        f.callback = cb ? 0x8804000 : 0;
        f.target = arguments[a] != 0;
        repops_me_startup_host h = {&f, rd, wr, svc};
        repops_me_wait w;
        repops_me_control_begin(&h, arguments[a], &w);
        assert(w.previous_ack_bit == (f.old_ack & 1));
        assert(f.events[0].kind == 1 && f.events[0].address == 0xBFC007F0);
        assert(f.events[1].kind == 2 && f.control == f.target);
        unsigned n = 0;
        while (!repops_me_wait_step(&h, &w)) assert(++n < 10);
        assert(f.delays == (cb ? d * 2 : 0));
        assert(f.polls == (cb ? d * 2 + 2 : 1));
        ++count;
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        fixture f = initial(0);
        f.no_ack = 1;
        f.target = kind == 2 ? 0 : 1;
        repops_me_startup_host h = {&f, rd, wr, svc};
        repops_me_wait w;
        if (kind == 0) repops_me_boot_begin(&h, &w);
        else repops_me_control_begin(&h, f.target, &w);
        for (unsigned i = 0; i < 16; ++i) assert(!repops_me_wait_step(&h, &w));
        assert(f.delays == 15 && w.phase == REPOPS_ME_DELAY_THEN_READ);
        ++count;
    }
    printf("ME startup/control: %u synthetic contract cases passed; no devices emulated.\n", count);
    return 0;
}
