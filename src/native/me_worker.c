#include "me_worker.h"
#include <stddef.h>

#define OUTPUT UINT32_C(0xBE000000)
#define REQUEST UINT32_C(0xBFC007F8)
#define ACK UINT32_C(0xBFC007F0)
#define TRANSFER UINT32_C(0xBC200000)

static int32_t signed_half(uint32_t word)
{
    const uint32_t half = word & UINT32_C(0xFFFF);
    return half < 0x8000 ? (int32_t)half : (int32_t)half - 0x10000;
}

static int32_t shift_right_two(int32_t value)
{
    /* MIPS SRA rounds negative values down, unlike C signed division. */
    return value >= 0 ? value / 4 : (int32_t)(-((-(int64_t)value + 3) / 4));
}

void rp_me_worker_init(rp_me_worker *worker)
{
    *worker = (rp_me_worker){.phase = RP_ME_ENTRY};
}

rp_me_step_result rp_me_worker_step(rp_me_worker *w, const rp_me_bus *b)
{
    if (!w || !b || !b->provider_read32 || !b->read32 || !b->read16 ||
        !b->write32 || !b->service || !b->sample) return RP_ME_INVALID_HOST;
    void *ctx = b->context;
    uint32_t value, control;
    switch (w->phase) {
    case RP_ME_ENTRY:
        b->write32(ctx, 0xBC000040, 0);
        value = b->read32(ctx, 0xBC000044);
        w->callback = b->provider_read32(ctx, 0x4C5C);
        b->write32(ctx, 0xBC000044, value & UINT32_C(0xFFFFFFC0));
        b->write32(ctx, 0xBC000000, 0x44444444);
        b->write32(ctx, 0xBC000004, 0x44);
        b->write32(ctx, 0xBC00000C, 0);
        /* The original rereads the register; do not assume it reads as zero. */
        value = b->read32(ctx, 0xBC00000C);
        b->write32(ctx, 0xBC000008, value);
        if (!w->callback) {
            w->phase = RP_ME_STOPPED;
            return RP_ME_PARKED;
        }
        b->write32(ctx, 0xBC300008, 0x1000);
        b->service(ctx, RP_ME_SYNC, 0);
        b->write32(ctx, OUTPUT, 1);
        w->phase = RP_ME_RESET_OUTPUT;
        return RP_ME_PROGRESS;
    case RP_ME_RESET_OUTPUT:
        b->write32(ctx, OUTPUT + 4, 0);
        w->phase = RP_ME_WAIT_IDLE;
        return RP_ME_PROGRESS;
    case RP_ME_WAIT_IDLE:
        if (b->read32(ctx, OUTPUT + 0xC) & 7) return RP_ME_WAITING_IO;
        w->phase = RP_ME_WAIT_CONFIG;
        return RP_ME_PROGRESS;
    case RP_ME_WAIT_CONFIG:
        if (b->read32(ctx, OUTPUT + 0x50) & 0x10000) {
            b->write32(ctx, 0xBFC00650, 0x2E);
            return RP_ME_WAITING_IO;
        }
        value = b->read32(ctx, 0xBFC007F4);
        b->write32(ctx, OUTPUT + 0x50, value);
        b->write32(ctx, OUTPUT + 0x2C, 7);
        b->write32(ctx, OUTPUT + 0x24, 0x22);
        b->write32(ctx, OUTPUT + 0x20, 7);
        b->write32(ctx, OUTPUT + 8, 0);
        b->write32(ctx, OUTPUT + 0x14, 0x1208);
        b->write32(ctx, OUTPUT + 0x18, 0);
        b->write32(ctx, OUTPUT + 0x10, 2);
        b->write32(ctx, OUTPUT + 0x38, 0x80);
        b->write32(ctx, OUTPUT + 0x40, 1);
        b->write32(ctx, OUTPUT + 4, 2);
        w->prefill_remaining = 24;
        w->phase = RP_ME_PREFILL;
        return RP_ME_PROGRESS;
    case RP_ME_PREFILL:
        if (!(b->read32(ctx, OUTPUT + 0x28) & 0x20)) return RP_ME_WAITING_IO;
        b->write32(ctx, OUTPUT + 0x70, 0);
        if (--w->prefill_remaining == 0) {
            w->packed = 0;
            w->phase = RP_ME_PUMP;
        }
        return RP_ME_PROGRESS;
    case RP_ME_PUMP:
        /* Host-scheduled polling adaptation: Allegrex HALT at +0x3100 is a
         * wake boundary, not Capstone MIPS32's apparent MADD instruction.
         * Interrupt delivery/timing is not reproduced by this state machine.
         */
        b->write32(ctx, TRANSFER + 4, 0x10004);
        b->write32(ctx, TRANSFER, 0x101FF);
        b->write32(ctx, TRANSFER, 0);
        b->write32(ctx, TRANSFER + 4, 0);
        value = b->read32(ctx, OUTPUT + 0x28);
        if (value & 2) {
            if (!b->read32(ctx, REQUEST)) {
                w->phase = RP_ME_RESET_OUTPUT;
                return RP_ME_PROGRESS;
            }
        } else if (!(value & 0x20)) return RP_ME_WAITING_IO;
        w->phase = RP_ME_EMIT_PREVIOUS;
        return RP_ME_PROGRESS;
    case RP_ME_EMIT_PREVIOUS:
        /* SW in JALR's delay slot: the old sample precedes the callback. */
        b->write32(ctx, OUTPUT + 0x70, w->packed);
        w->phase = RP_ME_CALL_SAMPLE;
        return RP_ME_PROGRESS;
    case RP_ME_CALL_SAMPLE:
        value = 0;
        if (!b->sample(ctx, w->callback, &value)) return RP_ME_CALLBACK_UNAVAILABLE;
        w->packed = value;
        control = b->read32(ctx, REQUEST);
        w->low_sample = signed_half(value);
        w->high_sample = signed_half(value >> 16);
        if (!control) w->phase = RP_ME_PUMP;
        else if (control & 2) {
            value = b->read16(ctx, control - UINT32_C(2));
            w->packed = value | (value << 16);
            w->phase = RP_ME_PUMP;
        } else {
            b->write32(ctx, TRANSFER, 0x10002);
            b->service(ctx, RP_ME_DCACHE_WRITEBACK_INVALIDATE, 0);
            w->phase = RP_ME_DRAIN;
        }
        return RP_ME_CALLBACK_RETURNED;
    case RP_ME_DRAIN:
        value = b->read32(ctx, OUTPUT + 0x28);
        if (!(value & 0x22)) return RP_ME_WAITING_IO;
        if (!(value & 2)) {
            w->low_sample -= shift_right_two(w->low_sample);
            w->high_sample -= shift_right_two(w->high_sample);
            value = ((uint32_t)w->low_sample & 0xFFFF) | ((uint32_t)w->high_sample << 16);
            b->write32(ctx, OUTPUT + 0x70, value);
        }
        control = b->read32(ctx, REQUEST);
        b->write32(ctx, ACK, control);
        if (!(control & 1)) {
            b->write32(ctx, TRANSFER, 0);
            if (control) {
                b->service(ctx, RP_ME_DDR_FLUSH, 8);
                w->phase = RP_ME_STOPPED;
            } else w->phase = RP_ME_RESET_OUTPUT;
        }
        return RP_ME_ACK_WRITTEN;
    case RP_ME_STOPPED:
        return RP_ME_PARKED;
    }
    return RP_ME_INVALID_HOST;
}
