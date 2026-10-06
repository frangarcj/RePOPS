#include "../src/native/pops_cdrom.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void rp_pops_graphics_event(rp_context *c, uint32_t cb) { (void)c; (void)cb; abort(); }
void rp_pops_initialize_core(rp_context *c) { (void)c; abort(); }
void rp_pops_invalidate_ram_code(rp_context *c) { (void)c; abort(); }
void rp_pops_prepare_exception(rp_context *c, uint32_t v) { (void)c; (void)v; abort(); }
/* Reset/audio pacing are integration dependencies, not exercised by this fixture. */
void rp_pops_cd_controller_reset(rp_context *c) { (void)c; abort(); }
void rp_pops_cd_audio_sync(rp_context *c) { (void)c; abort(); }
void rp_pops_audio_pace(rp_context *c) { (void)c; abort(); }

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c); c->gp = 0x10000; c->trace = tmpfile(); assert(c->trace);
    c->regions[0] = (rp_region){0, 0x100000, calloc(1, 0x100000)};
    c->regions[1] = (rp_region){0x09E80000, 0x1000, calloc(1, 0x1000)};
    c->regions[2] = (rp_region){0x09F40000, 0xC0000, calloc(1, 0xC0000)};
    assert(c->regions[0].bytes && c->regions[2].bytes);
    if (setjmp(c->stop)) { fprintf(stderr, "%s at %x\n", c->stop_kind, c->stop_address); return 1; }

    const uint32_t head = RP_CORE_CLOCK_ADDRESS(c, event_head_next);
    rp_w32(c, head, head); rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_head_prev), head);
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 0x100000);
    rp_core_set_downcount(c, 0x100000);
    rp_w8(c, RP_CD_ADDRESS(c, deferred_command), 0xFF);
    rp_w8(c, RP_CD_ADDRESS(c, drive_status), 2);
    rp_w32(c, RP_CD_ADDRESS(c, primary.event.callback), 0xC268);
    rp_w32(c, RP_CD_ADDRESS(c, secondary.event.callback), 0xC268);
    assert(rp_pops_cd_read(c, 0, 4) == 0x18);
    for (unsigned i = 0; i < 5; ++i) rp_pops_cd_write(c, 2, 0x40 + i);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, parameter_count)) == 4);
    assert(rp_u32(c, RP_CD_ADDRESS(c, parameters)) == 0x43424140);
    rp_pops_cd_write(c, 0, 1);
    rp_pops_cd_write(c, 3, 0x40);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, parameter_count)) == 0);
    rp_pops_cd_write(c, 2, 0x1F);
    rp_pops_cd_write(c, 0, 0);
    rp_pops_cd_write(c, 1, 1); /* Getstat queues a response at 0x4000 cycles. */
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, primary.pending_irq)) == 3);
    assert(rp_u32(c, RP_CD_ADDRESS(c, primary.event.deadline_cycles)) == 0x4000);
    assert(!(rp_pops_cd_read(c, 0, 4) & 0x20));
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, irq_flags)) == 0);

    /* A future event keeps the scheduler out of the separately unresolved empty sentinel. */
    rp_w32(c, RP_CD_ADDRESS(c, drive_event.callback), 0xCE00);
    rp_pops_schedule_event(c, RP_CD_ADDRESS(c, drive_event), 0x20000);
    rp_core_set_downcount(c, 0);
    (void)rp_pops_dispatch_events(c);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, irq_flags)) == 3);
    assert((rp_pops_cd_read(c, 0, 4) & 0xA0) == 0x20);
    assert(rp_u32(c, RP_DEVICE_ADDRESS(c, irq_status)) == 4);
    assert(rp_pops_cd_read(c, 1, 4) == 2);
    assert(!(rp_pops_cd_read(c, 0, 4) & 0x20));

    /* A second completed response must wait until the first IRQ is acknowledged. */
    rp_w8(c, RP_CD_ADDRESS(c, secondary.pending_irq), 2);
    rp_w8(c, RP_CD_ADDRESS(c, secondary.length), 1);
    rp_w8(c, RP_CD_ADDRESS(c, command_lock), 1);
    rp_pops_cd_event(c, RP_CD_ADDRESS(c, secondary), 0xC268);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, secondary.pending_irq)) == 2);
    rp_pops_cd_write(c, 0, 1);
    rp_pops_cd_write(c, 3, 3);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, irq_flags)) == 2);
    assert(!rp_cd_u8(c, RP_CD_ADDRESS(c, command_lock)));
    assert(rp_pops_cd_read(c, 3, 4) == 0xE2);
    assert(rp_pops_cd_read(c, 3, 0) == UINT32_C(0xFFFFFFE2));
    rp_pops_cd_write(c, 3, 2);

    rp_pops_cd_write(c, 0, 2);
    rp_pops_cd_write(c, 2, 0x11); rp_pops_cd_write(c, 3, 0x22);
    rp_pops_cd_write(c, 0, 3);
    rp_pops_cd_write(c, 1, 0x33); rp_pops_cd_write(c, 2, 0x44);
    assert(rp_u32(c, RP_CD_ADDRESS(c, volume_matrix)) == 0x44332211);

    /* Data reads advance then clamp at the last byte, clearing transfer flags. */
    const uint32_t sector = RP_SHARED_ADDRESS(sample_ram) + 0x2000;
    rp_w32(c, RP_CD_ADDRESS(c, sector_buffers[1]), sector);
    rp_w8(c, sector + 12, 0xA5); rp_w8(c, sector + 13, 0x5A);
    rp_w8(c, RP_CD_ADDRESS(c, mode), 0x20);
    rp_pops_cd_write(c, 0, 0); rp_pops_cd_write(c, 3, 0x80);
    rp_cd_w16(c, RP_CD_ADDRESS(c, data_limit), 14);
    assert(rp_pops_cd_read(c, 2, 4) == 0xA5);
    assert(rp_pops_cd_read(c, 2, 4) == 0x5A);

    const uint32_t cache = RP_DEVICE_ADDRESS(c, cd_cache);
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_cache_head), cache);
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_read_request), UINT32_MAX);
    rp_w32(c, RP_DEVICE_ADDRESS(c, disc_sector_limit), 1000);
    rp_w32(c, RP_FIELD_ADDRESS(cache, rp_cd_cache_node_layout, next), cache);
    rp_w32(c, RP_FIELD_ADDRESS(cache, rp_cd_cache_node_layout, prev), cache);
    rp_w32(c, RP_FIELD_ADDRESS(cache, rp_cd_cache_node_layout, first_sector), 0x80000000);
    rp_w8(c, RP_CD_ADDRESS(c, drive_status), 0x20);
    *(uint8_t *)rp_module_memory(c, 0xD47D4 + 2, 1) = 3;
    rp_pops_cd_write(c, 2, 0); rp_pops_cd_write(c, 2, 2); rp_pops_cd_write(c, 2, 4);
    rp_pops_cd_write(c, 1, 2);
    assert(rp_u32(c, RP_CD_ADDRESS(c, requested_sector)) == 4);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, location_pending)) == 1);
    assert(rp_u32(c, RP_DEVICE_ADDRESS(c, cd_read_request)) == 0);
    assert(c->cd_event_bits == 1);
    assert(rp_cd_u8(c, RP_CD_ADDRESS(c, irq_flags)) == 0);
    assert(!(rp_cd_u8(c, RP_CD_ADDRESS(c, data_request)) & 0x80));
    assert(rp_pops_cd_read(c, 2, 4) == 0x5A);

    rp_pops_remove_event(c, RP_CD_ADDRESS(c, drive_event));
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags), 3);
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_slow_seek_sector), UINT32_MAX);
    rp_w8(c, RP_CD_ADDRESS(c, saved_flag), 1);
    rp_w32(c, RP_CD_ADDRESS(c, current_sector), 7);
    rp_w32(c, RP_CD_ADDRESS(c, random_state), 0);
    assert(rp_pops_cd_seek_cycles(c, 7) == 2272);
    rp_w32(c, RP_CD_ADDRESS(c, current_sector), 0);
    rp_w32(c, RP_CD_ADDRESS(c, random_state), 0);
    assert(rp_pops_cd_seek_cycles(c, 7000) == 1188272); /* floor(sqrt(1000)) = 31. */
    rp_w8(c, RP_CD_ADDRESS(c, saved_flag), 0);
    assert(rp_pops_cd_seek_cycles(c, 7000) == 1);

    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[1].bytes); free(c->regions[2].bytes); free(c);
    puts("CD: banks, parameter FIFO, scheduled response, IRQ acknowledgement and data cursor passed.");
    return 0;
}
