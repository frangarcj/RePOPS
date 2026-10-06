#include "../src/native/pops_gpu.h"
#include "../src/native/pops_cdrom.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void reset_status(rp_context *c)
{
    memset(c->scratchpad, 0, sizeof(c->scratchpad));
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline), 10000);
    rp_core_set_downcount(c, 5000);
    rp_w32(c, RP_GPU_ADDRESS(c, frame_cycle_origin), 1000);
    rp_w32(c, RP_GPU_ADDRESS(c, status), 0x04000000);
    rp_w32(c, RP_GPU_ADDRESS(c, display_mode), 0xC100);
    rp_cd_w16(c, RP_GPU_ADDRESS(c, draw_mode), 0xF925);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles), 777);
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); assert(c->trace);
    c->gp = 0x10000;
    reset_status(c);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x84034125);
    assert(rp_core_downcount(c) == 4999);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles)) == 777);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles)) == 5001);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status)) == 0x04000000);

    /* Rapid polling debits the elapsed line phase but records pre-debit time. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, frame_cycle_origin), 4000);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), 4995);
    assert(rp_pops_gpu_read(c, 0x1F801814, 5) == 0x04034125);
    assert(rp_core_downcount(c) == 3998);
    assert(rp_u32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles)) == 5001);

    reset_status(c);
    rp_core_set_downcount(c, 0);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), 10000);
    (void)rp_pops_gpu_read(c, 0x1814, 2);
    assert(rp_core_downcount(c) == UINT32_MAX);

    reset_status(c);
    rp_w8(c, RP_GPU_ADDRESS(c, interlaced), 1);
    rp_w8(c, RP_GPU_ADDRESS(c, frame_phase), 1);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x84034125);
    rp_w32(c, RP_DEVICE_ADDRESS(c, compatibility_flags), 1u << 27);
    assert(rp_pops_gpu_read(c, 0x1814, 2) == 0x04034125);

    /* An unimplemented GPUREAD transfer must not return an invented word. */
    reset_status(c);
    rp_w32(c, RP_GPU_ADDRESS(c, data_read_cycle_cost), 7);
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_gpu_read(c, 0x1810, 2);
        assert(!"Unimplemented GPU data path returned");
    }
    assert(strcmp(c->stop_kind, "GPU_data_read_not_reconstructed") == 0);
    assert(c->stop_address == 0x130BC && rp_core_downcount(c) == 4993);
    fclose(c->trace); free(c);
    puts("GPU status: bit fields, polling debit, timestamps, interlace and data boundary passed.");
    return 0;
}
