#ifndef REPOPS_NATIVE_STATE_H
#define REPOPS_NATIVE_STATE_H
#include "runtime.h"

/* Wire-layout descriptions, never cast over guest memory. Field addresses use
 * offsetof; the existing accessors retain little-endian and alias semantics. */
typedef struct {
    uint8_t earlier_state[0x1AC];
    uint32_t event_deadline, event_downcount;
} rp_core_clock_layout;

typedef struct {
    int16_t level, target, step;
    uint16_t countdown, period;
} rp_volume_sweep_layout;

typedef struct {
    rp_volume_sweep_layout left, right;
    uint32_t raw_volume_pair;
} rp_master_volume_layout;

typedef struct {
    uint32_t work_area_base;
    int16_t iir_gain, comb_gain[4], wall_gain, allpass_gain[2];
} rp_reverb_parameters_layout;

typedef enum {
    RP_RV_SAME_WRITE, RP_RV_COMB0, RP_RV_COMB1, RP_RV_SAME_READ,
    RP_RV_DIFF_WRITE, RP_RV_COMB2, RP_RV_COMB3, RP_RV_DIFF_READ,
    RP_RV_APF1_WRITE, RP_RV_APF2_WRITE, RP_RV_APF1_READ, RP_RV_APF2_READ,
    RP_RV_TAP_COUNT
} rp_reverb_tap;

typedef struct {
    int32_t input_gain;
    uint32_t taps[RP_RV_TAP_COUNT];
    int32_t history[3];
} rp_reverb_channel_layout;

typedef struct {
    uint16_t volumes[2], pitch, start_address, adsr[2], envelope, repeat_address;
} rp_shared_voice_layout;

typedef struct {
    rp_shared_voice_layout voices[24];
    uint16_t master_volume_raw[2];
    int16_t reverb_volume[2];
    uint32_t key_on, key_off, pitch_modulation_mask, noise_mask, reverb_mask, end_mask;
    uint16_t unknown_1a0, reverb_base_units, irq_address_units, transfer_address_units;
    uint16_t transfer_data, control, transfer_control, status;
    int16_t cd_volume[2];
    uint8_t unknown_1b4[4];
    uint32_t effective_master_volume;
    uint8_t unknown_1bc[4];
    uint16_t allpass_delay_units[2];
    int16_t reverb_coefficients[8];
    uint16_t reverb_tap_units[10][2];
    int16_t reverb_input_gain[2];
    uint32_t effective_voice_volumes[24];
    uint32_t cd_buffers[8];
    uint32_t pending_key_on, pending_key_off, dirty_voice_mask, unknown_28c;
    uint16_t transfer_notification;
    uint8_t clear_irq_latch;
    int8_t cd_notification;
    uint32_t callback_count, unknown_298;
    uint16_t producer_state, consumer_state;
    uint32_t repeat_dirty_mask;
    uint8_t unknown_2a4[28];
    uint8_t sample_ram[0x80000];
} rp_me_shared_layout;

typedef struct {
    uint8_t coefficients_and_voices[0x1338]; /* Existing voice code migrates separately. */
    rp_master_volume_layout master_volume;
    rp_reverb_parameters_layout reverb_parameters;
    rp_reverb_channel_layout reverb_channels[2];
    uint32_t end_mask;
    uint16_t capture_cursor, cd_sample_offset;
    uint32_t cd_block_pointer;
    uint8_t unknown_13f0[0x390];
    uint32_t cd_stream_state, unknown_1784;
    uint8_t cd_active, unknown_1789;
    int16_t noise_sample, noise_countdown;
    uint8_t noise_phase, reverb_tail_compatibility;
    uint16_t previous_control, paired_block_address_units;
    int8_t paired_voice_index, relative_voice_index;
    uint8_t unknown_1796, irq_latch;
    uint32_t irq_cursor;
} rp_me_mixer_layout;

enum { RP_CAPTURE_SAMPLES = 512, RP_GUEST_SAMPLE_CYCLES = 0x300 };
#define RP_ME_SHARED_BASE UINT32_C(0x49F40000)
#define RP_ME_MIXER_BASE UINT32_C(0x09FF0000)
#define RP_FIELD_ADDRESS(base, type, member) ((uint32_t)(base) + (uint32_t)offsetof(type, member))
#define RP_CORE_CLOCK_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_clock_layout, member)
#define RP_SHARED_ADDRESS(member) RP_FIELD_ADDRESS(RP_ME_SHARED_BASE, rp_me_shared_layout, member)
#define RP_MIXER_ADDRESS(member) RP_FIELD_ADDRESS(RP_ME_MIXER_BASE, rp_me_mixer_layout, member)

static inline uint32_t rp_core_downcount(rp_context *c)
{
    return rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_downcount));
}
static inline void rp_core_set_downcount(rp_context *c, uint32_t count)
{
    rp_w32(c, RP_CORE_CLOCK_ADDRESS(c, event_downcount), count);
}
static inline uint32_t rp_core_guest_cycles(rp_context *c)
{
    return rp_u32(c, RP_CORE_CLOCK_ADDRESS(c, event_deadline)) - rp_core_downcount(c);
}
static inline uint32_t rp_reverb_channel_address(unsigned phase)
{
    return RP_MIXER_ADDRESS(reverb_channels) + phase * (uint32_t)sizeof(rp_reverb_channel_layout);
}
static inline uint32_t rp_capture_address(unsigned channel, uint32_t cursor)
{
    return RP_SHARED_ADDRESS(sample_ram) + (channel * RP_CAPTURE_SAMPLES + cursor) * sizeof(int16_t);
}

_Static_assert(offsetof(rp_core_clock_layout, event_downcount) == 0x1B0, "core clock layout");
_Static_assert(sizeof(rp_volume_sweep_layout) == 10, "volume sweep layout");
_Static_assert(sizeof(rp_master_volume_layout) == 24, "master volume layout");
_Static_assert(sizeof(rp_reverb_parameters_layout) == 20, "reverb parameters layout");
_Static_assert(sizeof(rp_reverb_channel_layout) == 64, "reverb channel layout");
_Static_assert(offsetof(rp_me_shared_layout, pending_key_on) == 0x280, "shared mailbox layout");
_Static_assert(offsetof(rp_me_shared_layout, sample_ram) == 0x2C0, "shared RAM prefix");
_Static_assert(sizeof(rp_me_shared_layout) == 0x802C0, "shared RAM layout");
_Static_assert(offsetof(rp_me_mixer_layout, reverb_channels) == 0x1364, "mixer channels layout");
_Static_assert(offsetof(rp_me_mixer_layout, irq_cursor) == 0x1798, "mixer IRQ layout");
_Static_assert(sizeof(rp_me_mixer_layout) == 0x179C, "mixer prefix layout");
#endif
