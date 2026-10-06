#include "runtime.h"
#include "pops_state.h"
#include <string.h>

#define SHARED UINT32_C(0x49F40000)
#define MIXER UINT32_C(0x09FF0000)

static uint16_t read_half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void write_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* POPS offset zero: entry -> +0x1968 -> +0x9D4, SPU disabled only.
 * This branch includes its state writes and return value; it is not a generic
 * silence stub. Enabled mixing and the producer busy-wait are not implemented.
 * Those unsupported domains are refused before touching shared producer state.
 */
bool rp_pops_spu_inactive_sample(rp_context *c, uint32_t *packed)
{
    rp_function(c, 0, "pops.spu_disabled_sample_path");
    const uint16_t control = read_half(c, SHARED + 0x1AA);
    if ((control & 0x8000) || (rp_u32(c, SHARED + 0x29C) & 0xFFFF) == 0x100)
        return false;

    write_half(c, SHARED + 0x29E, 1);
    rp_w32(c, SHARED + 0x294, rp_u32(c, SHARED + 0x294) + 1);
    uint32_t result = rp_u32(c, SHARED + 0x18C);
    rp_w8(c, SHARED + 0x29E, 0);
    const uint8_t notification = *(uint8_t *)rp_memory(c, SHARED + 0x293, 1);
    write_half(c, SHARED + 0x290, 0);
    if (notification == 0xFF) rp_w8(c, SHARED + 0x293, 0xFE);

    if (read_half(c, MIXER + 0x1790) & 0x8000) {
        write_half(c, MIXER + 0x1790, control & 0x3F);
        memset(rp_memory(c, MIXER + 0x858, 0xAE0), 0, 0xAE0);
        for (unsigned voice = 0; voice < 24; ++voice)
            write_half(c, SHARED + 0xC + voice * 0x10, 0);
        result = 0;
        rp_w8(c, MIXER + 0x1788, 0);
        rp_w32(c, MIXER + 0x1780, 0);
        rp_w32(c, MIXER + 0x13EC, 0);
        rp_w32(c, SHARED + 0x280, 0);
        rp_w32(c, SHARED + 0x284, 0);
    }

    const uint32_t old_index = read_half(c, MIXER + 0x13E8);
    const uint32_t cursor = rp_u32(c, MIXER + 0x1798);
    const uint32_t difference = cursor - (SHARED + 0x2C0 + old_index * 2);
    const uint8_t latch = *(uint8_t *)rp_memory(c, MIXER + 0x1797, 1) |
                           ((difference & UINT32_C(0xFFFFF3FF)) == 0);
    const uint32_t next_index = (old_index + 1) & 0x1FF;
    rp_w8(c, MIXER + 0x1797, latch);
    write_half(c, MIXER + 0x13E8, (uint16_t)next_index);
    rp_w32(c, SHARED + 0x19C, rp_u32(c, MIXER + 0x13E4));
    write_half(c, SHARED + 0x1AE,
               (uint16_t)((control & 0x3F) | ((latch != 0) << 6) | ((next_index >> 8) << 11)));
    *packed = result;
    rp_event(c, "milestone", "spu_disabled_callback_returned_with_state_updates", 0, result);
    return true;
}

static int32_t signed_half(uint32_t value)
{
    return (int32_t)(value & 0x7FFF) - (int32_t)(value & 0x8000);
}

/* Existing DSP code shares offsets derived from the recovered wire type. */
#define VOICE_FIELD(member) offsetof(rp_mixer_voice_layout, member)
enum {
    VOICE_STATE = VOICE_FIELD(envelope.phase), VOICE_EXPONENTIAL = VOICE_FIELD(envelope.exponential),
    VOICE_THRESHOLD = VOICE_FIELD(envelope.threshold), VOICE_COUNTDOWN = VOICE_FIELD(envelope.countdown),
    VOICE_STEP = VOICE_FIELD(envelope.step), VOICE_PERIOD = VOICE_FIELD(envelope.period),
    VOICE_LEVEL = VOICE_FIELD(envelope.level), VOICE_POSITION = VOICE_FIELD(sample_position),
    VOICE_BLOCK_ADDRESS = VOICE_FIELD(block_address), VOICE_MANUAL_LOOP = VOICE_FIELD(manual_repeat),
    VOICE_BLOCK_FLAGS = VOICE_FIELD(block_flags), VOICE_PITCH = VOICE_FIELD(pitch),
    VOICE_REPEAT_ADDRESS = VOICE_FIELD(repeat_address), VOICE_HISTORY = VOICE_FIELD(history),
    VOICE_SAMPLES = VOICE_FIELD(decoded), VOICE_STOPPED = VOICE_FIELD(stopped),
    VOICE_BYTES = sizeof(rp_mixer_voice_layout)
};

/* +0x1418..+0x1484, with the terminal release transition +0x16D8.
 * Other ADSR phase transitions remain explicit boundaries. */
static void advance_voice_envelope(rp_context *c, uint32_t voice, unsigned index)
{
    const int32_t state = (int8_t)*(uint8_t *)rp_memory(c, voice + VOICE_STATE, 1);
    if ((uint32_t)(state - 1) >= 27) return;
    const uint32_t remaining = (uint32_t)read_half(c, voice + VOICE_COUNTDOWN) - 1;
    write_half(c, voice + VOICE_COUNTDOWN, (uint16_t)remaining);
    if (remaining) return;

    const int32_t old = signed_half(read_half(c, voice + VOICE_LEVEL));
    const int32_t step = signed_half(read_half(c, voice + VOICE_STEP));
    const uint32_t exponential = *(uint8_t *)rp_memory(c, voice + VOICE_EXPONENTIAL, 1);
    int32_t next = exponential ? (old * step) >> 15 : old + step;
    const int32_t threshold = read_half(c, voice + VOICE_THRESHOLD);
    int32_t level = next;
    if (next == threshold || ((old - threshold) ^ (next - threshold)) < 0) {
        if (next > 0x7FFF) next = 0x7FFF;
        level = next < 0 ? 0 : next;
        switch (state) {
        case 17: case 19: case 24:
            rp_w8(c, voice + VOICE_STOPPED, 0xFF);
            /* These transitions converge at +0x1670. */
            /* fall through */
        case 16:
            write_half(c, voice + VOICE_PERIOD, 0);
            write_half(c, voice + VOICE_STEP, 0);
            rp_w8(c, voice + VOICE_EXPONENTIAL, 0);
            break;
        case 4: case 5: case 8: case 9: case 12: case 18: case 20:
            rp_event(c, "me_boundary", "envelope_transition_state", voice, (uint32_t)state);
            rp_block(c, "ME_envelope_phase_transition_not_reconstructed", 0x14C0);
        default: break;
        }
    }
    write_half(c, voice + VOICE_LEVEL, (uint16_t)level);
    if (*(uint8_t *)rp_memory(c, voice + VOICE_STOPPED, 1)) next = 0;
    write_half(c, SHARED + index * 0x10 + 0xC, (uint16_t)next);
    write_half(c, voice + VOICE_COUNTDOWN, read_half(c, voice + VOICE_PERIOD));
}

static int32_t clamp_sample(int32_t value)
{
    return value < -32768 ? -32768 : value > 32767 ? 32767 : value;
}

/* +0x1278..+0x139C: retain block flags/history and the original integer
 * predictor. This fills the voice's sample history, not a host audio buffer. */
static void decode_voice_block(rp_context *c, uint32_t voice, unsigned index)
{
    uint32_t address = read_half(c, voice + VOICE_BLOCK_ADDRESS);
    const uint32_t flags = *(uint8_t *)rp_memory(c, voice + VOICE_BLOCK_FLAGS, 1);
    if (flags & 1) {
        rp_w32(c, MIXER + 0x13E4, rp_u32(c, MIXER + 0x13E4) | (1u << index));
        address = read_half(c, voice + VOICE_REPEAT_ADDRESS);
        if (!(flags & 2)) {
            rp_w8(c, voice + VOICE_STOPPED, 1);
            write_half(c, SHARED + index * 0x10 + 0xC, 0);
        }
    }
    const uint32_t stream = SHARED + 0x2C0 + (address << 3);
    if (rp_u32(c, MIXER + 0x1798) - stream < 16)
        rp_block(c, "ME_ADPCM_IRQ_overlap_not_reconstructed", 0x13A0);
    const uint8_t *block = rp_memory(c, stream, 16);
    write_half(c, voice + VOICE_BLOCK_ADDRESS, (uint16_t)(address + 2));
    if ((block[1] & 6) == 6 && !*(uint8_t *)rp_memory(c, voice + VOICE_MANUAL_LOOP, 1)) {
        write_half(c, voice + VOICE_REPEAT_ADDRESS, (uint16_t)address);
        write_half(c, SHARED + index * 0x10 + 0xE, (uint16_t)address);
    }
    rp_w8(c, voice + VOICE_BLOCK_FLAGS, block[1] & 7);
    if ((int8_t)*(uint8_t *)rp_memory(c, voice + VOICE_STOPPED, 1) > 0) return;

    uint32_t filter = block[0] >> 4;
    if (filter >= 5) filter = 0;
    const int32_t positive = *(uint8_t *)rp_memory(c, MIXER + 0x846 + filter, 1);
    const int32_t negative = *(uint8_t *)rp_memory(c, MIXER + 0x84B + filter, 1);
    int32_t older = signed_half(read_half(c, voice + 0x6E));
    int32_t recent = signed_half(read_half(c, voice + 0x70));
    memcpy(rp_memory(c, voice + VOICE_HISTORY, 6), rp_memory(c, voice + 0x6C, 6), 6);
    const unsigned shift = (block[0] | 16u) & 31;
    for (unsigned i = 0; i < 28; ++i) {
        const unsigned nibble = (block[2 + i / 2] >> ((i & 1) * 4)) & 15;
        const int32_t raw = (int32_t)((uint32_t)nibble << 28) >> shift;
        const int32_t value = clamp_sample(raw + ((recent * positive - older * negative) >> 6));
        write_half(c, voice + VOICE_SAMPLES + i * 2, (uint16_t)value);
        older = recent; recent = value;
    }
    rp_event(c, "milestone", "ME_voice_ADPCM_block_decoded", stream, index);
}

/* +0x11DC..+0x1274: pitch, block refill and four-tap interpolation. */
static int32_t sample_voice(rp_context *c, uint32_t voice, unsigned index, int32_t previous)
{
    int32_t pitch = read_half(c, voice + VOICE_PITCH);
    if (rp_u32(c, SHARED + 0x190) & (1u << index))
        pitch = (int32_t)((uint32_t)((int64_t)pitch * ((int64_t)previous + 0x8000))) >> 15;
    if (pitch > 0x3FFF) pitch = 0x3FFF;
    uint32_t position = rp_u32(c, voice + VOICE_POSITION) + (uint32_t)pitch;
    if ((int32_t)position >= 0) {
        decode_voice_block(c, voice, index);
        position += UINT32_C(0xFFFE4000);
    }
    int32_t value = 0;
    if (rp_u32(c, SHARED + 0x194) & (1u << index)) {
        value = signed_half(read_half(c, MIXER + 0x178A));
    } else if (!*(uint8_t *)rp_memory(c, voice + VOICE_STOPPED, 1)) {
        const int32_t sample_index = (int32_t)position >> 12;
        const uint32_t history = voice + 0x6C + (uint32_t)(sample_index * 2);
        const uint32_t coefficients = MIXER + ((position >> 4) & 0xFF) * 8;
        uint32_t sum = 0;
        for (unsigned i = 0; i < 4; ++i)
            sum += (uint32_t)(signed_half(read_half(c, history + i * 2)) *
                              signed_half(read_half(c, coefficients + i * 2)));
        value = (int32_t)sum >> 15;
    }
    value = (int32_t)((uint32_t)((int64_t)value * signed_half(read_half(c, voice + VOICE_LEVEL)))) >> 15;
    rp_w32(c, voice + VOICE_POSITION, position);
    return value;
}

static int32_t multiply_q15(int32_t a, int32_t b)
{
    return (int32_t)(uint32_t)((int64_t)a * b) >> 15;
}

static uint32_t reverb_address(int32_t units)
{
    if (units > 0xFFFF) units = 0xFFFF;
    return RP_SHARED_ADDRESS(sample_ram) + (uint32_t)units * 8;
}

/* +0x434..+0x54C: parameter writes also rebuild both channel pointer sets. */
static void refresh_reverb_parameters(rp_context *c)
{
    const int32_t base = read_half(c, RP_SHARED_ADDRESS(reverb_base_units));
    memcpy(rp_memory(c, RP_MIXER_ADDRESS(reverb_parameters.iir_gain), 16),
           rp_memory(c, RP_SHARED_ADDRESS(reverb_coefficients), 16), 16);
    rp_w32(c, RP_MIXER_ADDRESS(reverb_parameters.work_area_base), reverb_address(base));
    for (unsigned i = 0; i < 10; ++i) {
        rp_w32(c, RP_MIXER_ADDRESS(reverb_channels[0].taps) + i * sizeof(uint32_t),
               reverb_address(base + read_half(c, RP_SHARED_ADDRESS(reverb_tap_units) + i * 4)));
        rp_w32(c, RP_MIXER_ADDRESS(reverb_channels[1].taps) + i * sizeof(uint32_t),
               reverb_address(base + read_half(c, RP_SHARED_ADDRESS(reverb_tap_units) + i * 4 + sizeof(uint16_t))));
    }
    for (unsigned channel = 0; channel < 2; ++channel) {
        const uint32_t state = rp_reverb_channel_address(channel);
        rp_w32(c, RP_FIELD_ADDRESS(state, rp_reverb_channel_layout, input_gain),
               (uint32_t)signed_half(read_half(c, RP_SHARED_ADDRESS(reverb_input_gain) + channel * sizeof(int16_t))));
        for (unsigned stage = 0; stage < 2; ++stage)
            rp_w32(c, RP_FIELD_ADDRESS(state, rp_reverb_channel_layout, taps[RP_RV_APF1_READ]) + stage * sizeof(uint32_t),
                   reverb_address(base + read_half(c, RP_SHARED_ADDRESS(reverb_tap_units[8]) + stage * 4 + channel * sizeof(uint16_t))
                                  - read_half(c, RP_SHARED_ADDRESS(allpass_delay_units) + stage * sizeof(uint16_t))));
    }
    if (*(uint8_t *)rp_memory(c, RP_MIXER_ADDRESS(reverb_tail_compatibility), 1))
        write_half(c, RP_MIXER_ADDRESS(capture_cursor), 0);
}

/* +0x550..+0x924: disabling reverb suppresses RAM writes, not the read/filter
 * path. Keep writes interleaved with reads because delay taps can alias. */
static void advance_reverb(rp_context *c, unsigned phase, bool enabled,
                           int32_t input, int32_t *left, int32_t *right)
{
    const uint32_t address = rp_reverb_channel_address(phase);
    const uint32_t taps = RP_FIELD_ADDRESS(address, rp_reverb_channel_layout, taps);
    const uint32_t history = RP_FIELD_ADDRESS(address, rp_reverb_channel_layout, history);
    const uint32_t wrap = RP_ME_SHARED_BASE + sizeof(rp_me_shared_layout);
    const uint32_t base = rp_u32(c, RP_MIXER_ADDRESS(reverb_parameters.work_area_base));
    const uint32_t irq = rp_u32(c, RP_MIXER_ADDRESS(irq_cursor));
    rp_reverb_channel_layout channel;
    channel.input_gain = (int32_t)rp_u32(c, RP_FIELD_ADDRESS(address, rp_reverb_channel_layout, input_gain));
    for (unsigned i = 0; i < RP_RV_TAP_COUNT; ++i) channel.taps[i] = rp_u32(c, taps + i * sizeof(uint32_t));
    for (unsigned i = 0; i < 3; ++i) channel.history[i] = (int32_t)rp_u32(c, history + i * sizeof(int32_t));
    bool irq_seen = irq == 0;
    for (unsigned i = 0; i < RP_RV_TAP_COUNT; ++i) irq_seen |= irq == channel.taps[i];

    if (enabled) {
        const int32_t driven = multiply_q15(clamp_sample(input), channel.input_gain);
        const int32_t wall = signed_half(read_half(c, RP_MIXER_ADDRESS(reverb_parameters.wall_gain)));
        const int32_t iir = signed_half(read_half(c, RP_MIXER_ADDRESS(reverb_parameters.iir_gain)));
        const rp_reverb_tap reads[] = {RP_RV_SAME_READ, RP_RV_DIFF_READ};
        const rp_reverb_tap writes[] = {RP_RV_SAME_WRITE, RP_RV_DIFF_WRITE};
        for (unsigned i = 0; i < 2; ++i) {
            const int32_t reflected = signed_half(read_half(c, channel.taps[reads[i]]));
            const int32_t old = signed_half(read_half(c, channel.taps[writes[i]]));
            uint32_t next = channel.taps[writes[i]] + sizeof(int16_t);
            if (next == wrap) next = base;
            const int32_t value = clamp_sample(old + multiply_q15(
                driven + multiply_q15(reflected, wall) - old, iir));
            write_half(c, next, (uint16_t)value);
        }
    }

    const unsigned combs[] = {RP_RV_COMB0, RP_RV_COMB1, RP_RV_COMB2, RP_RV_COMB3};
    int64_t sum = 0;
    for (unsigned i = 0; i < 4; ++i)
        sum += (int64_t)signed_half(read_half(c, channel.taps[combs[i]])) *
                       signed_half(read_half(c, RP_MIXER_ADDRESS(reverb_parameters.comb_gain) + i * sizeof(int16_t)));
    const int32_t first_old = signed_half(read_half(c, channel.taps[RP_RV_APF1_READ]));
    const int32_t first_gain = signed_half(read_half(c, RP_MIXER_ADDRESS(reverb_parameters.allpass_gain[0])));
    const int32_t second_gain = signed_half(read_half(c, RP_MIXER_ADDRESS(reverb_parameters.allpass_gain[1])));
    const int32_t first = clamp_sample((int32_t)(sum >> 15) - multiply_q15(first_old, first_gain));
    if (enabled) write_half(c, channel.taps[RP_RV_APF1_WRITE], (uint16_t)first);
    const int32_t second_old = signed_half(read_half(c, channel.taps[RP_RV_APF2_READ]));
    const int32_t second = clamp_sample(first_old + multiply_q15(first, first_gain)
                                       - multiply_q15(second_old, second_gain));
    if (enabled) write_half(c, channel.taps[RP_RV_APF2_WRITE], (uint16_t)second);
    int32_t signal = clamp_sample(second_old + multiply_q15(second, second_gain));
    if (!enabled && !*(uint8_t *)rp_memory(c, RP_MIXER_ADDRESS(reverb_tail_compatibility), 1)) signal = 0;

    for (unsigned i = 0; i < RP_RV_TAP_COUNT; ++i) {
        uint32_t next = channel.taps[i] + 2;
        if (next == wrap) next = base;
        if (i == RP_RV_SAME_WRITE || i == RP_RV_DIFF_WRITE) irq_seen |= irq == next;
        rp_w32(c, taps + i * sizeof(uint32_t), next);
    }
    if (irq_seen) rp_w8(c, RP_MIXER_ADDRESS(irq_latch), 1);
    const int32_t current = clamp_sample((int32_t)(uint32_t)(
        (int64_t)(channel.history[1] + channel.history[2]) * 15901 +
        (int64_t)(channel.history[0] + signal) * 418) >> 15);
    rp_w32(c, history, (uint32_t)channel.history[1]);
    rp_w32(c, history + sizeof(int32_t), (uint32_t)channel.history[2]);
    rp_w32(c, history + 2 * sizeof(int32_t), (uint32_t)signal);
    const uint32_t other = RP_FIELD_ADDRESS(rp_reverb_channel_address(phase ^ 1), rp_reverb_channel_layout, history);
    const int32_t a = (int32_t)rp_u32(c, other);
    const int32_t b = (int32_t)rp_u32(c, other + sizeof(int32_t));
    const int32_t d = (int32_t)rp_u32(c, other + 2 * sizeof(int32_t));
    const int32_t interpolated = clamp_sample((int32_t)(uint32_t)(
        (int64_t)(a + b) * 4839 + (int64_t)d * 22962) >> 15);
    *left = phase ? interpolated : current;
    *right = phase ? current : interpolated;
}

static uint32_t finish_active_mix(rp_context *c, uint16_t control, uint32_t dirty,
                                  uint32_t left, uint32_t right,
                                  uint32_t reverb_left, uint32_t reverb_right)
{
    if (!(control & 0x4000)) left = right = reverb_left = reverb_right = 0;
    const int32_t cd_state = (int8_t)*(uint8_t *)rp_memory(c, RP_SHARED_ADDRESS(cd_notification), 1);
    if (cd_state > 0)
        rp_block(c, "ME_CD_stream_mix_not_reconstructed", 0x2A0);
    if (cd_state == -1) {
        write_half(c, RP_SHARED_ADDRESS(transfer_notification), 0);
        rp_w32(c, RP_MIXER_ADDRESS(cd_stream_state), 0);
        rp_w8(c, RP_MIXER_ADDRESS(cd_active), 0);
        rp_w32(c, RP_MIXER_ADDRESS(cd_block_pointer), 0);
        rp_w8(c, RP_SHARED_ADDRESS(cd_notification), 0xFE);
    }
    const uint32_t cursor = read_half(c, RP_MIXER_ADDRESS(capture_cursor));
    write_half(c, rp_capture_address(0, cursor), 0);
    write_half(c, rp_capture_address(1, cursor), 0);
    const unsigned phase = cursor & 1;
    if (dirty & 0x80000000) refresh_reverb_parameters(c);
    int32_t wet_left, wet_right;
    advance_reverb(c, phase, (control & 0x80) != 0,
                   (int32_t)(phase ? reverb_right : reverb_left), &wet_left, &wet_right);
    left += (uint32_t)multiply_q15(wet_left, signed_half(read_half(c, RP_SHARED_ADDRESS(reverb_volume[0]))));
    right += (uint32_t)multiply_q15(wet_right, signed_half(read_half(c, RP_SHARED_ADDRESS(reverb_volume[1]))));

    const uint32_t master = RP_MIXER_ADDRESS(master_volume);
    const uint32_t volumes = rp_u32(c, RP_SHARED_ADDRESS(master_volume_raw));
    if (rp_u32(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, raw_volume_pair)) != volumes) {
        rp_w32(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, raw_volume_pair), volumes);
        if (volumes & 0x80008000)
            rp_block(c, "ME_master_volume_sweep_setup_not_reconstructed", 0xB48);
        write_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, left.level), (uint16_t)(volumes << 1));
        write_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, left.countdown), 0);
        write_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, right.level), (uint16_t)((volumes >> 16) << 1));
        write_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, right.countdown), 0);
    }
    if (read_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, left.countdown)) || read_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, right.countdown)))
        rp_block(c, "ME_master_volume_sweep_step_not_reconstructed", 0xADC);
    const int32_t gain_l = signed_half(read_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, left.level)));
    const int32_t gain_r = signed_half(read_half(c, RP_FIELD_ADDRESS(master, rp_master_volume_layout, right.level)));
    const int32_t sample_l = multiply_q15(clamp_sample((int32_t)left), gain_l);
    const int32_t sample_r = multiply_q15(clamp_sample((int32_t)right), gain_r);
    rp_w32(c, RP_SHARED_ADDRESS(effective_master_volume), (uint16_t)gain_l | ((uint32_t)(uint16_t)gain_r << 16));
    uint32_t packed = (uint16_t)sample_l | ((uint32_t)(uint16_t)sample_r << 16);
    if (!(control & 0x4003)) packed = 0;

    const uint32_t capture = read_half(c, RP_MIXER_ADDRESS(capture_cursor));
    const uint32_t irq_address = rp_u32(c, RP_MIXER_ADDRESS(irq_cursor));
    uint8_t irq_latch = *(uint8_t *)rp_memory(c, RP_MIXER_ADDRESS(irq_latch), 1);
    irq_latch |= ((irq_address - (rp_capture_address(0, capture))) & 0xFFFFF3FF) == 0;
    const uint32_t next_capture = (capture + 1) & (RP_CAPTURE_SAMPLES - 1);
    rp_w8(c, RP_MIXER_ADDRESS(irq_latch), irq_latch);
    write_half(c, RP_MIXER_ADDRESS(capture_cursor), (uint16_t)next_capture);
    rp_w32(c, RP_SHARED_ADDRESS(end_mask), rp_u32(c, RP_MIXER_ADDRESS(end_mask)));
    write_half(c, RP_SHARED_ADDRESS(status), (control & 0x3F) | ((irq_latch != 0) << 6) |
               ((next_capture >> 8) << 11));
    rp_event(c, "milestone", "ME_active_sample_completed", next_capture, packed);
    return packed;
}

/* Reached active path: voices followed by idle CD, reverb processing
 * and fixed master volume. Other routes retain explicit boundaries. */
static uint32_t active_sample(rp_context *c, uint16_t control)
{
    rp_function(c, 0, "pops.spu_active_sample_partial");
    write_half(c, SHARED + 0x29E, 1);
    rp_w32(c, SHARED + 0x294, rp_u32(c, SHARED + 0x294) + 1);
    const uint32_t dirty = rp_u32(c, SHARED + 0x288);
    const uint32_t repeat = rp_u32(c, SHARED + 0x2A0);
    const uint32_t keyon = rp_u32(c, SHARED + 0x280);
    const uint32_t keyoff = rp_u32(c, SHARED + 0x284);
    rp_w32(c, SHARED + 0x18C, rp_u32(c, SHARED + 0x18C) & ~keyon);
    rp_w32(c, SHARED + 0x288, 0); rp_w32(c, SHARED + 0x2A0, 0);
    rp_w32(c, SHARED + 0x280, 0); rp_w32(c, SHARED + 0x284, 0);
    rp_w32(c, SHARED + 0x188, 0);
    rp_w8(c, SHARED + 0x29E, 0);
    rp_w32(c, MIXER + 0x13E4, rp_u32(c, SHARED + 0x19C) & ~keyon);
    if (*(uint8_t *)rp_memory(c, SHARED + 0x292, 1)) {
        rp_w8(c, MIXER + 0x1797, 0); rp_w8(c, SHARED + 0x292, 0);
    }
    const uint32_t irq_cursor = control & 0x40 ?
        SHARED + 0x2C0 + ((uint32_t)read_half(c, SHARED + 0x1A4) << 3) : SHARED + 0x2BE;
    rp_w32(c, MIXER + 0x1798, irq_cursor);
    const uint16_t previous = read_half(c, MIXER + 0x1790);
    int32_t count = signed_half(read_half(c, MIXER + 0x178C));
    if ((control ^ previous) & 0x3F00) count = 0;
    --count;
    write_half(c, MIXER + 0x1790, control);
    if (count <= 0) {
        count = (int32_t)(0x4000u >> ((control >> 10) & 15));
        bool advance = count == 0;
        if (!advance) {
            const uint8_t phase = *(uint8_t *)rp_memory(c, MIXER + 0x178E, 1);
            const uint32_t taps = UINT32_C(0xFFFFEEAA) >> (((control >> 8) & 3) * 4);
            rp_w8(c, MIXER + 0x178E, (uint8_t)((phase >> 1) | (phase << 7)));
            advance = (phase & taps) != 0;
        }
        if (advance) {
            const int32_t noise = signed_half(read_half(c, MIXER + 0x178A));
            const uint32_t feedback = (0x69u >> (((uint32_t)noise >> 10) & 7)) ^
                                       (uint32_t)(noise >> 15);
            write_half(c, MIXER + 0x178A, (uint16_t)(((uint32_t)noise << 1) | (feedback & 1)));
        }
    }
    write_half(c, MIXER + 0x178C, (uint16_t)count);
    rp_event(c, "milestone", "ME_active_mailbox_consumed", dirty, keyon);

    int32_t previous_sample = 0;
    uint32_t left_sum = 0, right_sum = 0, reverb_left = 0, reverb_right = 0;
    uint32_t reverb = rp_u32(c, SHARED + 0x198);
    /* Control and sample processing are interleaved for each voice, as in
     * +0x1B4..+0x284; never consume all voice controls ahead of their samples. */
    for (unsigned index = 0; index < 24; ++index) {
        const uint32_t bit = 1u << index;
        const uint32_t voice = RP_MIXER_ADDRESS(voices[index]);
        const uint32_t registers = RP_SHARED_ADDRESS(voices[index]);
        bool envelope_changed = false;
        if (dirty & bit) {
            write_half(c, voice + 0x30, read_half(c, registers + 4));
            if (repeat & bit) {
                rp_w8(c, voice + 0x2E, 1);
                write_half(c, voice + 0x32, read_half(c, registers + 0xE) & 0xFFFE);
            }
            const uint32_t configuration = rp_u32(c, RP_SHARED_ADDRESS(voices[index].adsr));
            const uint32_t saved_configuration = RP_MIXER_ADDRESS(voices[index].envelope.configuration);
            envelope_changed = rp_u32(c, saved_configuration) != configuration;
            /* +0x180C's taken delay slot clears only this voice's dirty sign
             * when ADSR is unchanged. Pitch/volume writes must not reset it. */
            if (envelope_changed) rp_w32(c, saved_configuration, configuration);
            const uint32_t volumes = rp_u32(c, registers);
            if (volumes != rp_u32(c, voice + 0x14)) {
                rp_w32(c, voice + 0x14, volumes);
                if (volumes & 0x8000) rp_block(c, "ME_left_volume_sweep_not_reconstructed", 0x18D4);
                write_half(c, voice, (uint16_t)(volumes << 1));
                write_half(c, voice + 6, 0);
                if (volumes & UINT32_C(0x80000000))
                    rp_block(c, "ME_right_volume_sweep_not_reconstructed", 0x1854);
                write_half(c, voice + 0xA, (uint16_t)((volumes >> 16) << 1));
                write_half(c, voice + 0x10, 0);
            }
        }
        int32_t state = (int8_t)*(uint8_t *)rp_memory(c, voice + 0x1C, 1);
        if (keyon & bit) {
            write_half(c, registers + 0xC, 0);
            rp_w32(c, voice + 0x2C, read_half(c, registers + 6) & 0xFFFE);
            write_half(c, voice + 0x26, 0); rp_w32(c, voice + 0x28, 0);
            if ((int8_t)*(uint8_t *)rp_memory(c, MIXER + 0x1794, 1) == (int)index) {
                const int32_t relative = (int8_t)*(uint8_t *)rp_memory(c, MIXER + 0x1795, 1);
                const uint32_t paired = voice + (uint32_t)(relative * 0x74);
                rp_w32(c, paired + 0x28, UINT32_C(0xFFFE4000));
                write_half(c, paired + 0x2C, read_half(c, MIXER + 0x1792));
                rp_w8(c, MIXER + 0x1797, 0);
            }
            rp_w32(c, voice + 0x20, 4);
            rp_w32(c, voice + 0x70, 0); rp_w32(c, voice + 0x6C, 0);
            state = 5; rp_w32(c, voice + 0x1C, 5);
        }
        const bool release_started = (keyoff & bit) && state < 24;
        if (release_started) {
            const uint32_t adsr2 = read_half(c, voice + 0x1A);
            const int32_t shift = (int32_t)(adsr2 & 31) - 11;
            uint32_t step = 0xFFF8, period = 1;
            rp_w8(c, voice + 0x1C, 24); write_half(c, voice + 0x1E, 0);
            if (shift > 0) {
                period = (UINT32_C(1) << shift) & 0xFFFF;
                if (!period) step = 0;
            } else {
                const unsigned rotate = (uint32_t)shift & 31;
                step = (step >> rotate) | (step << ((32 - rotate) & 31));
            }
            write_half(c, voice + 0x24, (uint16_t)period);
            const uint32_t exponential = step ? (rp_u32(c, voice + 0x18) >> 21) & 1 : 0;
            if (exponential) step &= 0x7FF8;
            write_half(c, voice + 0x22, (uint16_t)step);
            write_half(c, voice + 0x20, (uint16_t)period);
            rp_w8(c, voice + 0x1D, (uint8_t)exponential);
            rp_event(c, "milestone", "ME_voice_release_initialized", voice, period);
        }
        const int32_t remaining = (int32_t)read_half(c, voice + VOICE_COUNTDOWN) - 1;
        int32_t sample = 0;
        if (!release_started && remaining > 0 && state < 6) {
            write_half(c, voice + VOICE_COUNTDOWN, (uint16_t)remaining);
        } else {
            if (!release_started && remaining > 0 && envelope_changed && !(keyon & bit)) {
                const uint16_t level = read_half(c, voice + VOICE_LEVEL);
                write_half(c, voice + VOICE_COUNTDOWN, 1);
                write_half(c, voice + VOICE_THRESHOLD, level);
                rp_w8(c, voice + VOICE_STATE, (uint8_t)((state & ~3) - 4));
                rp_w8(c, voice + VOICE_EXPONENTIAL, 0);
                write_half(c, voice + VOICE_STEP, 0);
                rp_event(c, "milestone", "ME_voice_envelope_reconfigured", index, (uint32_t)state);
            }
            advance_voice_envelope(c, voice, index);
            if (index == 0 && (rp_u32(c, SHARED + 0x190) & 1))
                rp_block(c, "ME_first_voice_pitch_modulation_not_reconstructed", 0x1408);
            reverb = (reverb >> 1) | (reverb << 31);
            sample = sample_voice(c, voice, index, previous_sample);
        }
        if (read_half(c, voice + 6) || read_half(c, voice + 0x10))
            rp_block(c, "ME_volume_sweep_step_not_reconstructed", 0x1158);
        const int32_t left = signed_half(read_half(c, voice));
        const int32_t right = signed_half(read_half(c, voice + 0xA));
        if (index == 1 || index == 3) {
            const uint32_t capture = (index << 8) + read_half(c, MIXER + 0x13E8);
            write_half(c, SHARED + 0x8C0 + capture * 2, (uint16_t)sample);
        }
        const int32_t l = (int32_t)((uint32_t)((int64_t)sample * left)) >> 15;
        const int32_t r = (int32_t)((uint32_t)((int64_t)sample * right)) >> 15;
        left_sum += (uint32_t)l; right_sum += (uint32_t)r;
        if (reverb & 0x80000000) { reverb_left += (uint32_t)l; reverb_right += (uint32_t)r; }
        rp_w32(c, SHARED + 0x200 + index * 4, (uint16_t)left | ((uint32_t)(uint16_t)right << 16));
        previous_sample = sample;
        rp_event(c, "milestone", "ME_voice_sample_accumulated", voice, (uint32_t)sample);
    }
    rp_event(c, "milestone", "ME_24_voice_loop_complete", left_sum, right_sum);
    rp_event(c, "milestone", "ME_voice_reverb_inputs", reverb_left, reverb_right);
    return finish_active_mix(c, control, dirty, left_sum, right_sum, reverb_left, reverb_right);
}

bool rp_pops_spu_sample(rp_context *c, uint32_t *packed)
{
    const uint16_t control = read_half(c, SHARED + 0x1AA);
    if (!(control & 0x8000)) return rp_pops_spu_inactive_sample(c, packed);
    if ((rp_u32(c, SHARED + 0x29C) & 0xFFFF) == 0x100) return false;
    *packed = active_sample(c, control);
    return true;
}
