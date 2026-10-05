#include "me_startup.h"

#define ACK UINT32_C(0xBFC007F0)
#define CONTROL UINT32_C(0xBFC007F8)

/* Provider +0x35D8..+0x36D7. Calls and MMIO stay in instruction order. */
void repops_me_boot_begin(const repops_me_startup_host *h, repops_me_wait *w)
{
    h->service(h->context, REPOPS_ME_AVC_RESET, 0, 0, 0);
    h->service(h->context, REPOPS_ME_RESET_ENABLE, 0, 0, 0);
    h->service(h->context, REPOPS_ME_BUS_CLOCK, 0, 0, 0);
    const uint32_t intr = h->service(h->context, REPOPS_ME_SUSPEND_INTR, 0, 0, 0);
    const uint32_t clock = h->read32(h->context, UINT32_C(0xBC100070));
    h->write32(h->context, UINT32_C(0xBC100070), clock & ~UINT32_C(4));
    h->service(h->context, REPOPS_ME_RESUME_INTR, intr, 0, 0);
    h->service(h->context, REPOPS_ME_COPY, UINT32_C(0xBFC00040), 0x2F28, 0x60);

    const uint32_t config = h->read32(h->context, 0x4A08);
    const uint32_t first_word = h->read32(h->context, UINT32_C(0xBFC00040));
    h->write32(h->context, UINT32_C(0xBFC00040), first_word | (config & 3));
    h->service(h->context, REPOPS_ME_CACHE_WRITEBACK, 0, 0, 0);
    h->service(h->context, REPOPS_ME_DDR_FLUSH, 4, 0, 0);

    const uint32_t mode = h->read32(h->context, 0x4C68);
    h->write32(h->context, CONTROL, (mode & 2) != 0 ? mode : 1);
    h->write32(h->context, ACK, 0);
    h->service(h->context, REPOPS_ME_RESET_RELEASE, 0, 0, 0);
    *w = (repops_me_wait){1, 0, REPOPS_ME_FIRST_READ};
}

/* +0x3514..+0x358F, sceMeAudio_68C55F4C. Semantic name is provisional. */
void repops_me_control_begin(const repops_me_startup_host *h, uint32_t argument,
                             repops_me_wait *w)
{
    const uint32_t target = argument != 0;
    const uint32_t old_ack = h->read32(h->context, ACK);
    h->write32(h->context, CONTROL, target);
    h->service(h->context, REPOPS_ME_CODEC_376399B6, target ^ 1, 0, 0);
    const uint32_t callback = h->read32(h->context, 0x4C5C);
    *w = (repops_me_wait){target, old_ack & 1,
        callback == 0 ? REPOPS_ME_ACKNOWLEDGED : REPOPS_ME_FIRST_READ};
}

bool repops_me_wait_step(const repops_me_startup_host *h, repops_me_wait *w)
{
    if (w->phase == REPOPS_ME_ACKNOWLEDGED) return true;
    if (w->phase == REPOPS_ME_DELAY_THEN_READ) {
        h->service(h->context, REPOPS_ME_DELAY, 100, 0, 0);
    }
    if (h->read32(h->context, ACK) == w->target) {
        w->phase = REPOPS_ME_ACKNOWLEDGED;
        return true;
    }
    w->phase = REPOPS_ME_DELAY_THEN_READ;
    return false;
}
