#include "runtime.h"
#include "pops_cdrom.h"
#include "pops_gpu.h"
#include "pops_display.h"
#include <math.h>
#include <string.h>

static uint16_t half(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static void put_half(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void copy_template(rp_context *c, uint32_t destination, uint32_t source, uint32_t size)
{
    memcpy(rp_memory(c, destination, size), rp_module_memory(c, source, size), size);
}

/* Readback differs from headless list capture: the caller will consume the
 * pixels immediately. Never acknowledge this dependency using untouched
 * zero-initialized EDRAM. The actual GE backend must implement these barriers. */
uint32_t rp_ge_readback_restart_list(rp_context *c, uint32_t old_list)
{
    rp_event(c, "GPU_readback_boundary", "sceGeListSync_before_restart", old_list, 0x49A00000);
    rp_block(c, "GE_readback_previous_list_execution_required", 0x133AC);
}

uint32_t rp_ge_readback_barrier(rp_context *c, uint32_t old_list, uint32_t continuation)
{
    rp_event(c, "GPU_readback_boundary", "POPSMAN_7014C540_execution_required", old_list, continuation);
    rp_block(c, "GE_readback_execution_required", 0x13148);
}
static void copy_template_table(rp_context *c, uint32_t destination,
                                uint32_t table, unsigned count, int optional)
{
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t source = rp_module_u32(c, table + i * 4);
        if (!source && optional) continue;
        copy_template(c, destination + i * 0x40, source, 0x40);
    }
}

/* Headless capture of the two bounded, state-only startup lists. All words,
 * including repeated matrix writes, are retained in order for a later backend.
 * Capturing these lists is not GPU execution. Drawing/transfer/control-flow
 * commands are refused rather than silently treated as completed rendering.
 */
uint32_t rp_ge_capture_state_list(rp_context *c, uint32_t address, int module_relative)
{
    const uint32_t start = c->ge_command_count;
    for (unsigned words = 0; words < 512; ++words) {
        const uint32_t pc = address + words * 4;
        const uint32_t word = module_relative ? rp_module_u32(c, pc) : rp_u32(c, pc);
        const uint32_t opcode = word >> 24;
        if ((opcode < 0x10 && opcode != 0 && opcode != 0xF && opcode != 0xC) || opcode == 0xEA)
            rp_block(c, "GE_draw_or_control_flow_not_reconstructed", pc);
        if (c->ge_command_count >= 512) rp_block(c, "GE_startup_capture_full", pc);
        c->ge_commands[c->ge_command_count++] = word;
        if (word == 0x0C000000) {
            ++c->ge_lists_captured; ++c->services;
            rp_event(c, "headless_adapter", "GE_state_list_captured_not_rendered", address,
                     c->ge_command_count - start);
            return ++c->next_id;
        }
    }
    rp_block(c, "GE_initial_list_end_not_found", address);
}

/* +0x25344 maps the stored display choice to a small table index. */
static uint32_t display_choice(rp_context *c)
{
    rp_function(c, 0x25344, "pops.display_choice_index");
    switch (rp_u32(c, 0x163220)) {
    case 10: return 0;
    case 11: return 1;
    case 12: return 3;
    case 27: return 2;
    case 78: return 4;
    default: return UINT32_MAX;
    }
}

static void graphics_tables(rp_context *c)
{
    if (c->regions[3].size != 0x400000)
        rp_block(c, "graphics_EDRAM_allocation_variant_not_supported", 0x1B9C4);
    ++c->services;
    rp_event(c, "host_adapter", "provider_set_get_shadow_EDRAM_size", 0x2AC64C3F, 0x400000);
    uint32_t storage = 0x04360000;
    for (unsigned bank = 0; bank < 2; ++bank) {
        for (unsigned group = 0; group < 15; group += 3) {
            for (unsigned plane = 0; plane < 3; ++plane) {
                const unsigned index = bank * 16 + group + plane;
                rp_w32(c, RP_GPU_ADDRESS(c, texture_cache[index].storage_address), storage + plane * 0x80);
                rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[index].group_offset), (uint8_t)plane);
                rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[index].cache_flags), 0);
                put_half(c, RP_GPU_ADDRESS(c, texture_cache[index].group_x_origin),
                         (uint16_t)(plane == 0 ? group << 6 : 0));
            }
            storage += 0x20000;
        }
        const unsigned last = bank * 16 + 15;
        rp_w32(c, RP_GPU_ADDRESS(c, texture_cache[last].storage_address),
               rp_u32(c, RP_GPU_ADDRESS(c, texture_cache[last - 1].storage_address)) + 0x80);
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[last].group_offset), 3);
        rp_w8(c, RP_GPU_ADDRESS(c, texture_cache[last].cache_flags), 0);
        put_half(c, RP_GPU_ADDRESS(c, texture_cache[last].group_x_origin), 0);
        storage = 0x04100000;
    }
    uint32_t mode = display_choice(c);
    if (mode > 4) mode = 0;
    rp_w8(c, c->gp + 0x3663, (uint8_t)mode);
    rp_w8(c, c->gp + 0x3666, 0);
    c->ge_edram_translation = 0x200;
    ++c->services;
    rp_event(c, "host_adapter", "GE_EDRAM_translation_recorded", 0, 0x200);
    for (unsigned i = 0; i < 256; ++i) rp_w32(c, 0x041AD800 + i * 4, i * UINT32_C(0x01010101));
    for (unsigned strip = 0; strip < 10; ++strip) {
        for (unsigned plane = 0; plane < 3; ++plane) {
            const uint32_t p = 0x041BBA10 + (strip * 3 + plane) * 20;
            put_half(c, p - 16, (uint16_t)plane);
            put_half(c, p - 6, (uint16_t)(plane + 0x2C0));
            put_half(c, p - 4, (uint16_t)(0xF << (plane * 4)));
            put_half(c, p - 12, (uint16_t)(strip * 0x40));
            put_half(c, p - 2, (uint16_t)((strip + 1) * 0x40));
            put_half(c, p, 0x200);
        }
    }
    copy_template_table(c, 0x49B7F800, 0xD52BC, 16, 0);
    copy_template_table(c, 0x49B7FE00, 0xD5318, 8, 1);
    copy_template_table(c, 0x49B7FD00, 0xD52FC, 4, 0);
    copy_template_table(c, 0x49B7FC40, 0xD530C, 3, 0);
    static const uint32_t copies[][3] = {
        {0x49B7FC00,0xD4A38,0x18},{0x041B92C0,0xD4FE4,0x24},
        {0x441B9000,0xD4F88,0x34},{0x441B9040,0xD4FBC,0x28},
        {0x441B9080,0xD4EEC,0x40},{0x441B90C0,0xD4F6C,0x1C},
        {0x441B9100,0xD4F2C,0x40},{0x041B9300,0xD51B4,0x4C},
        {0x041B9200,0xD5150,100}
    };
    for (unsigned i = 0; i < sizeof(copies) / sizeof(copies[0]); ++i)
        copy_template(c, copies[i][0], copies[i][1], copies[i][2]);
    rp_function(c, 0x28290, "pops.get_background_level");
    uint32_t background = rp_u32(c, 0x163234);
    if (background & 0x80000000) background = 0;
    rp_w32(c, 0x041B9200, (background * 0x101010 & 0xFFFFFF) | 0x55000000);
    copy_template(c, 0x041B9400, 0xD5200, 0xBC);
    static const uint32_t commands[] = {
        0xEA000001,0xCB000000,0xCC000000,0x28000000,0x04060002,0x28000001,0x04060004
    };
    for (unsigned strip = 0; strip < 10; ++strip)
        for (unsigned word = 0; word < 7; ++word)
            rp_w32(c, 0x041B94C4 + strip * 0x20 + word * 4, commands[word]);
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t p = 0x041BBD00 + i * 12;
        put_half(c, p + 6, (uint16_t)((i + 1) * 64));
        put_half(c, p, (uint16_t)(i * 64));
        put_half(c, p + 2, 0); put_half(c, p + 4, 0);
        put_half(c, p + 8, 0x200); put_half(c, p + 10, 0);
    }
    rp_pops_config_postprocess(c);
    rp_event(c, "headless_adapter", "graphics_cache_flush_elided", 0x1B9C4, 0);
    rp_event(c, "milestone", "graphics_tables_and_command_templates_prepared", 0x041B9000, 0);
}

static void schedule_event(rp_context *c, uint32_t event, uint32_t delay)
{
    rp_pops_schedule_event(c, event, delay);
}

/* +0x30C24's idle-UI path. Headless services report no HOME/power events;
 * non-idle paths remain explicit rather than inventing a menu transition.
 */
static uint32_t poll_idle_ui(rp_context *c)
{
    rp_function(c, 0x30C24, "pops.poll_idle_UI_path");
    if (*(uint8_t *)rp_memory(c, 0x450C14, 1) || *(uint8_t *)rp_memory(c, 0x4A0C10, 1))
        rp_block(c, "UI_storage_event_not_reconstructed", 0x30C24);
    ++c->services;
    rp_event(c, "headless_adapter", "impose_and_power_no_pending_events", 0x30C24, 0);
    rp_w32(c, 0x14D0C0, 0);
    if (rp_u32(c, 0x14D090)) rp_block(c, "active_UI_state_not_reconstructed", 0x30C24);
    rp_w32(c, 0x14D0BC, 0);
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t address = 0x14D0B4 + i * 4, value = rp_u32(c, address);
        if ((int32_t)value > 0) {
            rp_w32(c, address, value - 1);
            if (value == 1) rp_block(c, "UI_countdown_callback_not_reconstructed", 0x1A8A8);
        }
    }
    if (rp_u32(c, 0x14D094) || rp_u32(c, 0x14D0B0))
        rp_block(c, "UI_transition_not_reconstructed", 0x30C24);
    return 0;
}

/* +0x1B7C8. The provider display-mode request is captured, not sent to a GPU. */
static void display_mode(rp_context *c, uint32_t mode)
{
    rp_function(c, 0x1B7C8, "pops.set_display_mode");
    if ((int32_t)mode < 0) mode = (rp_u32(c, c->gp + 0x3668) >> 13) & 1;
    rp_w8(c, c->gp + 0x365E, (uint8_t)mode);
    if (!rp_u32(c, 0x14D090)) {
        const uint32_t state = rp_u32(c, 0x49CBD4);
        if (!state && mode == 1) rp_w32(c, 0x49CBD4, 1);
        else if (state == 1 && !mode) rp_w32(c, 0x49CBD4, 2);
    }
    ++c->services;
    rp_event(c, "headless_adapter", "display_mode_request_captured", mode ? 0 : 0xE0, 0x01E00110);
}

static uint32_t graphics_word(rp_context *c, uint32_t cursor, uint32_t word)
{
    rp_w32(c, cursor, word);
    return cursor + 4;
}

static void finish_refresh_timing(rp_context *c, uint32_t next_frame, bool release_stall)
{
    ++c->services;
    rp_event(c, "headless_adapter", "virtual_vblank_at_requested_frame_no_realtime_wait",
             next_frame, 0);
    if (release_stall) {
        c->ge_stalled_list = 0;
        ++c->services;
        rp_event(c, "headless_adapter", "GE_stall_release_captured_not_rendered",
                 0xE7F06E2B, 0);
    }
    rp_w32(c, RP_GPU_ADDRESS(c, frame_counter), next_frame);
    rp_w32(c, RP_GPU_ADDRESS(c, audio_sample_origin),
           rp_u32(c, RP_SHARED_ADDRESS(callback_count)));
    rp_w32(c, RP_GPU_ADDRESS(c, audio_cycle_origin), rp_core_guest_cycles(c));
    rp_w8(c, RP_GPU_ADDRESS(c, previous_field), 0);
    const uint8_t idle = *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, display_mode), 1);
    if (idle < 128) {
        rp_w8(c, RP_GPU_ADDRESS(c, display_mode), (uint8_t)(idle - 1));
        if (!idle) {
            ++c->services;
            rp_event(c, "headless_adapter", "impose_power_tick_request", 0x1A23C094, 0);
        }
    }
    const uint32_t rate = rp_u32(c, RP_DISPLAY_CONFIG(c, frame_rate_ratio));
    if ((int32_t)rate > 0) {
        const uint32_t numerator = (rate >> 16) & 0x7FFF, denominator = rate & 0xFFFF;
        rp_w32(c, RP_GPU_ADDRESS(c, display_rate_remaining), denominator - numerator);
        if (rp_u32(c, RP_GPU_ADDRESS(c, earlier_event.prev)))
            rp_block(c, "display_timer_unlink_not_reconstructed", 0x9668);
        if (!denominator) rp_block(c, "display_rate_zero_divisor", 0x115B4);
        schedule_event(c, RP_GPU_ADDRESS(c, earlier_event),
                       (0x89D00 / denominator) * numerator);
    }
}

/* +0x115B4: internal-screen refresh, without an active UI. This
 * constructs the actual GE words but does not render or invent a framebuffer.
 * Device/display services are explicit headless adapters, not PSP timing.
 */
static void refresh_display(rp_context *c)
{
    rp_function(c, 0x115B4, "pops.refresh_display_partial");
    const uint32_t status = rp_u32(c, RP_GPU_ADDRESS(c, status));
    if (!(status & 0x04000000) && *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, command_mode), 1) == 8) {
        rp_w8(c, RP_GPU_ADDRESS(c, refresh_on_ready), 1);
        return;
    }
    const uint32_t mode = rp_u32(c, RP_GPU_ADDRESS(c, display_mode)), flags = rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags));
    const uint8_t old_field = *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, previous_field), 1);
    rp_w8(c, RP_GPU_ADDRESS(c, refresh_on_ready), 0);
    rp_w8(c, RP_GPU_ADDRESS(c, display_mode_gate), (uint8_t)(((mode >> 10) & 1) & ((mode >> 13) & 1)));
    if (*(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, interlaced), 1) != ((mode >> 13) & 1)) {
        display_mode(c, (mode >> 13) & 1);
        rp_w8(c, RP_GPU_ADDRESS(c, external_field_mode), *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, external_field_mode), 1) & 1);
    }
    const uint32_t next_frame = rp_pops_display_next_frame(c);
    const uint8_t dirty = *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, display_dirty), 1);
    if (flags & 0x8000) {
        if (old_field || dirty) rp_w8(c, RP_GPU_ADDRESS(c, display_mode_bytes[2]), 0);
        else {
            const uint8_t count = *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, display_mode_bytes[2]), 1);
            if (count == 60) rp_block(c, "display_idle_callback_not_reconstructed", 0x1AF14);
            rp_w8(c, RP_GPU_ADDRESS(c, display_mode_bytes[2]), (uint8_t)(count + 1));
        }
    }
    ++c->services;
    rp_event(c, "headless_adapter", "impose_service_54F2AE52_return_unused", 0x2270, 0);
    (void)poll_idle_ui(c);
    const uint8_t initialized = *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, display_initialized), 1);
    if (!initialized) {
        rp_w8(c, RP_GPU_ADDRESS(c, display_initialized), 0xFF);
        rp_function(c, 0x34350, "pops.restore_display_buffer");
        ++c->services;
        rp_event(c, "headless_adapter", "display_buffer_request_captured", 0x041BC000, 512);
    }
    if ((mode & 0x1000) || *(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, external_output), 1))
        rp_block(c, "display_24bit_or_external_path_not_reconstructed", 0x115B4);
    if (initialized && !old_field && !dirty) {
        /* +0x1252C waits until the requested vcount and jumps directly into
         * the timing tail. The headless harness reaches that vblank without
         * wall-clock sleeping; no GE list is submitted on this path. */
        finish_refresh_timing(c, next_frame, false);
        rp_event(c, "milestone", "repeat_idle_refresh_timing_only", next_frame, 0);
        return;
    }
    if (*(uint8_t *)rp_memory(c, RP_GPU_ADDRESS(c, ge_transfer_pending), 1) & 0x80) {
        c->services += 2;
        rp_event(c, "headless_adapter", "previous_GE_list_sync_completed", 0x12504,
                 rp_u32(c, RP_GPU_ADDRESS(c, list_id)));
        rp_w32(c, RP_GPU_ADDRESS(c, list_id), ++c->next_id);
        rp_event(c, "headless_adapter", "GE_list_enqueue_captured", 0x49A00000,
                 c->next_id);
    }
    uint32_t out = rp_u32(c, RP_GPU_ADDRESS(c, list_cursor)), start = out;
    if (!(status & 0x800000)) {
        out = rp_pops_display_active_lists(c, out);
    } else {
        out = graphics_word(c, out, 0x13041B92);
        out = graphics_word(c, out, 0x0A000000);
        out = graphics_word(c, out, 0x0A0000C0);
    }

    /* The reset texture cache has no dirty entries. Any occupied entry that
     * would require the relocation/copy path is still a separate boundary.
     */
    bool free_slot = false;
    for (unsigned group = 0; group < 2; ++group)
        for (unsigned row = 0; row < 5; ++row) {
            const unsigned index = group * 16 + row * 3;
            if (!rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[index].cache_flags)) &&
                    !(rp_u32(c, RP_GPU_ADDRESS(c, texture_cache[index].storage_address)) & 0x08000000) &&
                    (!rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[index + 3].group_offset)) ||
                     !rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[index + 3].cache_flags))))
                free_slot = true;
        }
    if (free_slot)
        for (unsigned group = 0; group < 2; ++group)
            for (unsigned row = 0; row < 5; ++row) {
                const unsigned index = group * 16 + row * 3;
                if (rp_cd_u8(c, RP_GPU_ADDRESS(c, texture_cache[index].cache_flags)) &
                        (rp_u32(c, RP_GPU_ADDRESS(c, texture_cache[index].storage_address)) >> 27))
                    rp_block(c, "texture_cache_relocation_not_reconstructed", 0x11B54);
            }
    put_half(c, RP_GPU_ADDRESS(c, draw_mode), half(c, RP_GPU_ADDRESS(c, draw_mode)) | 0xC000);
    rp_w32(c, RP_GPU_ADDRESS(c, list_cursor), 0x49A00000);
    rp_w8(c, RP_GPU_ADDRESS(c, texture_depth), 0xFF); rp_w8(c, RP_GPU_ADDRESS(c, ge_transfer_pending), 0xFF);
    const uint32_t x0 = (uint32_t)(int32_t)(int16_t)half(c, RP_GPU_ADDRESS(c, draw_area_start[0]));
    const uint32_t y0 = (uint32_t)(int32_t)(int16_t)half(c, RP_GPU_ADDRESS(c, draw_area_start[1]));
    const uint32_t x1 = (uint32_t)(int32_t)(int16_t)half(c, RP_GPU_ADDRESS(c, draw_area_end[0]));
    const uint32_t y1 = (uint32_t)(int32_t)(int16_t)half(c, RP_GPU_ADDRESS(c, draw_area_end[1]));
    out = graphics_word(c, out, 0xD4000000 | x0 | y0 << 10);
    out = graphics_word(c, out, 0xD5000000 | x1 | y1 << 10);
    out = graphics_word(c, out, 0x13041B93);
    out = graphics_word(c, out, 0x0A000000);
    finish_refresh_timing(c, next_frame, true);
    rp_event(c, "milestone", status & 0x800000 ? "first_disabled_display_command_sequence" :
             "active_display_refresh_command_sequence_not_rendered", start, (out - start) / 4);
}

/* +0x11410, first-frame path. ROUND.W.S uses nearest/even. */
static void begin_frame(rp_context *c)
{
    rp_function(c, 0x11410, "pops.begin_frame_prefix");
    rp_w8(c, c->gp + 0x3662, (uint8_t)(*(uint8_t *)rp_memory(c, c->gp + 0x3662, 1) + 1));
    rp_function(c, 0x9940, "pops.timer_gate_disabled_path");
    if (rp_u32(c, c->gp + 0x684) & 1) rp_block(c, "active_timer_gate_not_reconstructed", 0x9940);
    const uint32_t mode = rp_u32(c, c->gp + 0x3668);
    const uint32_t pal = (mode >> 11) & 1, interlace = (mode >> 13) & 1;
    const int lines = (int)((0x6467 >> (interlace * 8)) & (0x81 - pal * 2))
                      - 2 * ((int)half(c, c->gp + 0x361A) - (int)half(c, c->gp + 0x3618)) + 0x20D;
    const uint32_t bits = pal ? 0x448779A7 : 0x448688CE;
    float factor; memcpy(&factor, &bits, 4);
    const float product = (float)lines * factor;
    double rounded = floor((double)product);
    const double fraction = (double)product - rounded;
    if (fraction > 0.5 || (fraction == 0.5 && fmod(rounded, 2.0) != 0)) rounded += 1.0;
    if (rounded < 0 || rounded > INT32_MAX) rp_block(c, "frame_delay_domain_not_supported", 0x11410);
    rp_w32(c, c->gp + 0x35F0, 0x1265C);
    schedule_event(c, c->gp + 0x35E4, (uint32_t)rounded);
    rp_pops_raise_irq(c, 1);
    rp_event(c, "milestone", "first_guest_frame_event_scheduled", 0x1265C, (uint32_t)rounded);
    if ((uint32_t)half(c, c->gp + 0x3610) + (uint32_t)half(c, c->gp + 0x3612) * 0x400 ==
            rp_u32(c, c->gp + 0x35C8) || (rp_u32(c, c->gp + 0x6AC) & 0x20000000)) {
        if (!(mode & 0x800) || !(rp_u32(c, c->gp + 0x6AC) & 8)) {
            rp_w8(c, c->gp + 0x3667, 1);
            refresh_display(c);
        }
    }
}

/* +0x15F54 switches the shared frame event back to +0x11410. */
static void finish_frame_phase(rp_context *c, uint32_t delay)
{
    rp_function(c, 0x15F54, "pops.finish_frame_phase");
    const uint32_t gp = c->gp;
    rp_w32(c, gp + 0x35F0, 0x11410);
    schedule_event(c, gp + 0x35E4, delay);
    const uint32_t debit = rp_u32(c, gp + 0x1E4);
    rp_w32(c, gp + 0x1E4, 0);
    rp_w32(c, gp + 0x1B0, rp_u32(c, gp + 0x1B0) - debit);
    if (!*(uint8_t *)rp_memory(c, gp + 0x3667, 1) &&
            (!(rp_u32(c, gp + 0x3668) & 0x800) || !(rp_u32(c, gp + 0x6AC) & 8)))
        refresh_display(c);
    rp_w8(c, gp + 0x3667, 0);
    rp_w32(c, gp + 0x35C8, half(c, gp + 0x3610) + ((uint32_t)half(c, gp + 0x3612) << 10));
}

/* +0x1265C. Like +0x11410, the multiply is single precision and ROUND.W.S
 * rounds ties to even. The optional early phase is measured in guest cycles.
 */
static void advance_frame_phase(rp_context *c)
{
    rp_function(c, 0x1265C, "pops.advance_frame_phase");
    const uint32_t gp = c->gp, early = rp_u32(c, gp + 0x708);
    rp_function(c, 0x9940, "pops.timer_gate_disabled_path");
    if (rp_u32(c, gp + 0x684) & 1) rp_block(c, "active_timer_gate_not_reconstructed", 0x9940);
    const int lines = (int)half(c, gp + 0x361A) - (int)half(c, gp + 0x3618);
    const uint32_t bits = rp_u32(c, gp + 0x3668) & 0x800 ? 0x450779A7 : 0x450688CE;
    float factor;
    memcpy(&factor, &bits, sizeof(factor));
    const float product = (float)lines * factor;
    double rounded = floor((double)product);
    const double fraction = (double)product - rounded;
    if (fraction > 0.5 || (fraction == 0.5 && fmod(rounded, 2.0) != 0)) rounded += 1.0;
    if (rounded < 0 || rounded > INT32_MAX) rp_block(c, "frame_phase_delay_domain", 0x1265C);
    rp_w8(c, gp + 0x3662, (uint8_t)(*(uint8_t *)rp_memory(c, gp + 0x3662, 1) + 1));
    rp_w32(c, gp + 0x35C4, rp_u32(c, gp + 0x35EC));
    if ((int32_t)early > 0) {
        rp_w32(c, gp + 0x35F0, 0x15FE4);
        rp_w32(c, gp + 0x35F4, (uint32_t)rounded - early);
        schedule_event(c, gp + 0x35E4, early);
    } else {
        finish_frame_phase(c, (uint32_t)rounded);
    }
}

void rp_pops_graphics_event(rp_context *c, uint32_t callback)
{
    switch (callback) {
    case 0x125F0: {
        rp_function(c, 0x125F0, "pops.GPU_ready_event_partial");
        const uint32_t status = RP_GPU_ADDRESS(c, status);
        rp_w32(c, status, rp_u32(c, status) | 0x14000000);
        if (rp_cd_u8(c, RP_GPU_ADDRESS(c, refresh_on_ready)))
            refresh_display(c);
        else if ((int8_t)rp_cd_u8(c, RP_GPU_ADDRESS(c, ge_transfer_pending)) > 0)
            rp_pops_gpu_submit_pending_list(c);
        rp_event(c, "milestone", "GPU_ready_event_completed", callback, rp_u32(c, status));
        return;
    }
    case 0x1265C: advance_frame_phase(c); return;
    case 0x11410: begin_frame(c); return;
    case 0x15FE4:
        rp_function(c, 0x15FE4, "pops.finish_delayed_frame_phase");
        finish_frame_phase(c, rp_u32(c, c->gp + 0x35F4));
        return;
    default: rp_block(c, "guest_event_callback_not_reconstructed", callback);
    }
}

void rp_pops_graphics_initialize(rp_context *c)
{
    rp_function(c, 0x1B9C4, "pops.graphics_initialization_prefix");
    ++c->services;
    rp_event(c, "headless_adapter", "impose_service_54F2AE52_return_unused", 0x2270, 0);
    if (!rp_u32(c, c->gp + 0x3400)) graphics_tables(c);
    const uint32_t groups[] = {0x35F8,0x35D0,0x35E4};
    for (unsigned i = 0; i < 3; ++i) memset(rp_memory(c, c->gp + groups[i], 16), 0, 16);
    rp_w32(c, c->gp + 0x35DC, 0x12710);
    rp_w32(c, c->gp + 0x3604, 0x125F0);
    rp_w8(c, c->gp + 0x3662, 0);
    rp_pops_gpu_write(c, 0x1F801814, 0);
    const uint32_t output = rp_u32(c, c->gp + 0x362C);
    static const uint32_t commands[] = {0xD4000000,0xD507FFFF,0x55000000,0x13041B90,0x0A000040};
    for (unsigned i = 0; i < 5; ++i) rp_w32(c, output + i * 4, commands[i]);
    rp_w32(c, c->gp + 0x362C, output + 20);
    rp_event(c, "milestone", "graphics_command_buffer_initialized", output, 5);
    begin_frame(c);
    rp_pops_install_dma(c, 2, 0x12C74);
    rp_pops_map_io(c, 0x1F801810, 8, 0x12FBC, 0x127D8);
    rp_w32(c, 0x49CBD4, 0);
    rp_event(c, "milestone", "GPU_IO_and_DMA_registered", 0x1F801810, 2);
}
