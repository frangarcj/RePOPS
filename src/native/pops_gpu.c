#include "pops_gpu.h"
#include "pops_cdrom.h"

#define GPU32(member) rp_u32(c, RP_GPU_ADDRESS(c, member))
#define GPU8(member) rp_cd_u8(c, RP_GPU_ADDRESS(c, member))
#define GPU16(member) rp_cd_u16(c, RP_GPU_ADDRESS(c, member))

/* +0x12FBC..+0x130BC: status reads compose existing state and frame timing.
 * The polling debit and two timestamps are observable firmware behavior;
 * no GPU-ready bit or frame completion is supplied by the host. */
uint32_t rp_pops_gpu_read(rp_context *c, uint32_t address, uint32_t width)
{
    (void)width; /* Original always returns one word, independent of this input. */
    rp_function(c, 0x12FBC, "pops.gpu_register_read_partial");
    if (!(address & 4)) {
        rp_core_set_downcount(c, rp_core_downcount(c) - GPU32(data_read_cycle_cost));
        rp_block(c, "GPU_data_read_not_reconstructed", 0x130BC);
    }

    uint32_t remaining = rp_core_downcount(c) - 1;
    const uint32_t deadline = rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline));
    const uint32_t now = deadline - remaining;
    rp_core_set_downcount(c, remaining);
    if (now - GPU32(status_poll_previous_cycles) < 30) {
        const int32_t phase_cycles = (int32_t)((now - GPU32(frame_cycle_origin)) & 0x7FF);
        const int32_t available = (int32_t)remaining;
        const int32_t debit = phase_cycles < available ? phase_cycles : available;
        if (debit > 0) {
            remaining -= (uint32_t)debit;
            rp_core_set_downcount(c, remaining);
        }
    }

    const uint32_t mode = GPU32(display_mode);
    const uint32_t inverted_phase = ~((uint32_t)GPU8(frame_phase));
    uint32_t status = GPU32(status);
    status = (status & ~UINT32_C(0x007E0000)) | (((mode >> 8) & 63) << 17);
    status = (status & ~UINT32_C(0x00010000)) | (((mode >> 14) & 1) << 16);
    status = (status & ~UINT32_C(0x00004000)) | (((mode >> 15) & 1) << 14);
    status = (status & ~UINT32_C(0x7FF)) | (GPU16(draw_mode) & 0x7FF);
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_previous_cycles), GPU32(status_poll_last_cycles));
    rp_w32(c, RP_GPU_ADDRESS(c, status_poll_last_cycles), now);

    uint32_t field;
    if (!GPU8(interlaced)) {
        field = ((deadline - remaining - GPU32(frame_cycle_origin)) >> 11) & inverted_phase;
    } else {
        field = (uint32_t)((int32_t)inverted_phase >> 1);
        if (rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags)) & (UINT32_C(1) << 27))
            field &= inverted_phase;
    }
    status = (status & UINT32_C(0x7FFFFFFF)) | ((field & 1) << 31);
    rp_event(c, "GPU_register_read", "status_from_state_and_guest_cycles", address, status);
    return status;
}
