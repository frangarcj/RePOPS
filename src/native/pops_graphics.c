#include "runtime.h"
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
static uint32_t capture_init_list(rp_context *c, uint32_t address, int module_relative)
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
                const uint32_t row = c->gp + 0x3400 + (bank * 16 + group + plane) * 8;
                rp_w32(c, row, storage + plane * 0x80);
                rp_w8(c, row + 5, (uint8_t)plane);
                rp_w8(c, row + 4, 0);
                put_half(c, row + 6, (uint16_t)(plane == 0 ? group << 6 : 0));
            }
            storage += 0x20000;
        }
        const uint32_t base = c->gp + bank * 0x80;
        rp_w32(c, base + 0x3478, rp_u32(c, base + 0x3470) + 0x80);
        rp_w8(c, base + 0x347D, 3);
        rp_w8(c, base + 0x347C, 0);
        put_half(c, base + 0x347E, 0);
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

/* +0x127D8, control-port command zero only. No GP0 primitive decoder yet. */
static void gpu_control_reset(rp_context *c)
{
    rp_function(c, 0x127D8, "pops.gpu_control_reset_path");
    if (rp_u32(c, 0x49CBD4) == 1) rp_w32(c, 0x49CBD4, 2);
    rp_w8(c, c->gp + 0x3658, 0);
    if (rp_u32(c, c->gp + 0x35CC)) rp_block(c, "existing_GE_list_reset_not_reconstructed", 0x127D8);
    (void)capture_init_list(c, 0xD5008, 1);
    (void)capture_init_list(c, 0x041B9300, 0);
    rp_w32(c, c->gp + 0x3630, 0x1C800000);
    put_half(c, c->gp + 0x3654, 0xC000);
    put_half(c, c->gp + 0x3614, 0x200); put_half(c, c->gp + 0x3616, 0xC00);
    put_half(c, c->gp + 0x3618, 0x10); put_half(c, c->gp + 0x361A, 0x100);
    rp_w8(c, c->gp + 0x365F, 2);
    rp_w32(c, c->gp + 0x362C, 0x49A00000);
    rp_w8(c, c->gp + 0x365E, 0xFF); rp_w8(c, c->gp + 0x3656, 0xFF);
    rp_w8(c, c->gp + 0x365D, 0xFF); rp_w8(c, c->gp + 0x3653, 0x20);
    rp_w8(c, c->gp + 0x3669, 0);
    for (unsigned i = 0; i < 6; ++i) put_half(c, c->gp + 0x3620 + i * 2, 0);
    put_half(c, c->gp + 0x3612, 0); put_half(c, c->gp + 0x3610, 0);
    rp_w8(c, c->gp + 0x3652, 0x20); rp_w8(c, c->gp + 0x3650, 0);
    rp_w8(c, c->gp + 0x3651, 0);
    ++c->services;
    rp_event(c, "headless_adapter", "GE_sync_captured_state_lists_only", 0, c->ge_lists_captured);
    c->ge_stalled_list = 0x49A00000;
    rp_w32(c, c->gp + 0x35CC, ++c->next_id);
    ++c->services;
    rp_event(c, "headless_adapter", "GE_empty_list_queued_at_stall", c->ge_stalled_list, c->next_id);
    rp_w8(c, c->gp + 0x3657, 0); rp_w8(c, c->gp + 0x366B, 0); rp_w8(c, c->gp + 0x366C, 0);
}

/* +0x945C, insert a guest event into the existing sorted intrusive list. */
static void schedule_event(rp_context *c, uint32_t event, uint32_t delay)
{
    rp_function(c, 0x945C, "pops.schedule_guest_event");
    const uint32_t downcount = rp_u32(c, c->gp + 0x1B0);
    const uint32_t deadline = rp_u32(c, c->gp + 0x1AC) - downcount + delay;
    const uint32_t head = c->gp + 0x1B8;
    uint32_t previous = head, next = rp_u32(c, head);
    unsigned visited = 0;
    while (next != head && (int32_t)(rp_u32(c, next + 8) - deadline) <= 0) {
        if (++visited > 1024) rp_block(c, "guest_event_list_cycle", next);
        previous = next; next = rp_u32(c, next);
    }
    rp_w32(c, event, next); rp_w32(c, event + 4, previous);
    rp_w32(c, previous, event); rp_w32(c, next + 4, event);
    if ((int32_t)downcount > 0 && previous == head) {
        rp_w32(c, c->gp + 0x1B0, delay); rp_w32(c, c->gp + 0x1AC, deadline);
    }
    rp_w32(c, event + 8, deadline);
}
static void raise_irq(rp_context *c, uint32_t bits)
{
    rp_function(c, 0x96E4, "pops.raise_interrupt_bits");
    const uint32_t old = rp_u32(c, c->gp + 0x2070);
    const uint32_t next = old | bits;
    if (old == next) return;
    rp_w32(c, c->gp + 0x2070, next);
    if (!(bits & rp_u32(c, c->gp + 0x2074))) return;
    const uint32_t cause = rp_u32(c, c->gp + 0x134) | 0x400;
    rp_w32(c, c->gp + 0x134, cause);
    const uint32_t status = rp_u32(c, c->gp + 0x130);
    if ((status & 1) && (cause & status & 0xFF00)) {
        const uint32_t downcount = rp_u32(c, c->gp + 0x1B0);
        rp_w32(c, c->gp + 0x1B0, 0);
        rp_w32(c, c->gp + 0x1AC, rp_u32(c, c->gp + 0x1AC) - downcount);
    }
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

/* +0x115B4: the reset path with PS1 display disabled and no active UI. This
 * constructs the actual GE words but does not render or invent a framebuffer.
 * Device/display services are explicit headless adapters, not PSP timing.
 */
static void refresh_disabled_display(rp_context *c)
{
    rp_function(c, 0x115B4, "pops.refresh_disabled_display_path");
    const uint32_t gp = c->gp, status = rp_u32(c, gp + 0x3630);
    if (!(status & 0x04000000) && *(uint8_t *)rp_memory(c, gp + 0x3657, 1) == 8) {
        rp_w8(c, gp + 0x3661, 1);
        return;
    }
    const uint32_t mode = rp_u32(c, gp + 0x3668), flags = rp_u32(c, gp + 0x6AC);
    const uint8_t old_field = *(uint8_t *)rp_memory(c, gp + 0x3660, 1);
    rp_w8(c, gp + 0x3661, 0);
    rp_w8(c, gp + 0x365D, (uint8_t)(((mode >> 10) & 1) & ((mode >> 13) & 1)));
    if (*(uint8_t *)rp_memory(c, gp + 0x365E, 1) != ((mode >> 13) & 1)) {
        display_mode(c, (mode >> 13) & 1);
        rp_w8(c, gp + 0x3665, *(uint8_t *)rp_memory(c, gp + 0x3665, 1) & 1);
    }
    const uint32_t next_frame = rp_u32(c, gp + 0x35C0) + 1;
    if (!(flags & 8) && (mode & 0x800))
        rp_block(c, "PAL_frame_correction_not_reconstructed", 0x125A4);
    const uint8_t dirty = *(uint8_t *)rp_memory(c, gp + 0x365F, 1);
    if (flags & 0x8000) {
        if (old_field || dirty) rp_w8(c, gp + 0x366A, 0);
        else {
            const uint8_t count = *(uint8_t *)rp_memory(c, gp + 0x366A, 1);
            if (count == 60) rp_block(c, "display_idle_callback_not_reconstructed", 0x1AF14);
            rp_w8(c, gp + 0x366A, (uint8_t)(count + 1));
        }
    }
    ++c->services;
    rp_event(c, "headless_adapter", "impose_service_54F2AE52_return_unused", 0x2270, 0);
    (void)poll_idle_ui(c);
    const uint8_t initialized = *(uint8_t *)rp_memory(c, gp + 0x3666, 1);
    if (!initialized) {
        rp_w8(c, gp + 0x3666, 0xFF);
        rp_function(c, 0x34350, "pops.restore_display_buffer");
        ++c->services;
        rp_event(c, "headless_adapter", "display_buffer_request_captured", 0x041BC000, 512);
    }
    if (!(status & 0x800000) || (mode & 0x1000) ||
            *(uint8_t *)rp_memory(c, gp + 0x3664, 1))
        rp_block(c, "active_display_refresh_not_reconstructed", 0x115B4);
    if (initialized && !old_field && !dirty)
        rp_block(c, "repeat_idle_refresh_not_reconstructed", 0x1252C);
    if (*(uint8_t *)rp_memory(c, gp + 0x3658, 1) & 0x80)
        rp_block(c, "GE_previous_frame_sync_not_reconstructed", 0x12504);
    uint32_t out = rp_u32(c, gp + 0x362C), start = out;
    out = graphics_word(c, out, 0x13041B92);
    out = graphics_word(c, out, 0x0A000000);
    out = graphics_word(c, out, 0x0A0000C0);

    /* The reset texture cache has no dirty entries. Any occupied entry that
     * would require the relocation/copy path is still a separate boundary.
     */
    bool free_slot = false;
    for (unsigned group = 0; group < 2; ++group)
        for (unsigned row = 0; row < 5; ++row) {
            const uint32_t p = gp + 0x3400 + group * 0x80 + row * 24;
            if (!*(uint8_t *)rp_memory(c, p + 4, 1) && !(rp_u32(c, p) & 0x08000000) &&
                    (!*(uint8_t *)rp_memory(c, p + 29, 1) || !*(uint8_t *)rp_memory(c, p + 28, 1)))
                free_slot = true;
        }
    if (free_slot)
        for (unsigned group = 0; group < 2; ++group)
            for (unsigned row = 0; row < 5; ++row) {
                const uint32_t p = gp + 0x3400 + group * 0x80 + row * 24;
                if (*(uint8_t *)rp_memory(c, p + 4, 1) & (rp_u32(c, p) >> 27))
                    rp_block(c, "texture_cache_relocation_not_reconstructed", 0x11B54);
            }
    put_half(c, gp + 0x3654, half(c, gp + 0x3654) | 0xC000);
    rp_w32(c, gp + 0x362C, 0x49A00000);
    rp_w8(c, gp + 0x3656, 0xFF); rp_w8(c, gp + 0x3658, 0xFF);
    const uint32_t x0 = (uint32_t)(int32_t)(int16_t)half(c, gp + 0x3620);
    const uint32_t y0 = (uint32_t)(int32_t)(int16_t)half(c, gp + 0x3622);
    const uint32_t x1 = (uint32_t)(int32_t)(int16_t)half(c, gp + 0x3624);
    const uint32_t y1 = (uint32_t)(int32_t)(int16_t)half(c, gp + 0x3626);
    out = graphics_word(c, out, 0xD4000000 | x0 | y0 << 10);
    out = graphics_word(c, out, 0xD5000000 | x1 | y1 << 10);
    out = graphics_word(c, out, 0x13041B93);
    out = graphics_word(c, out, 0x0A000000);
    ++c->services;
    rp_event(c, "headless_adapter", "virtual_vblank_at_requested_frame_no_realtime_wait", next_frame, 0);
    c->ge_stalled_list = 0;
    ++c->services;
    rp_event(c, "headless_adapter", "GE_stall_release_captured_not_rendered", 0xE7F06E2B, 0);
    rp_w32(c, gp + 0x35C0, next_frame);
    rp_w32(c, gp + 0x3608, rp_u32(c, 0x49F40294));
    rp_w32(c, gp + 0x360C, rp_u32(c, gp + 0x1AC) - rp_u32(c, gp + 0x1B0));
    rp_w8(c, gp + 0x3660, 0);
    const uint8_t idle = *(uint8_t *)rp_memory(c, gp + 0x3668, 1);
    if (idle < 128) {
        rp_w8(c, gp + 0x3668, (uint8_t)(idle - 1));
        if (!idle) {
            ++c->services;
            rp_event(c, "headless_adapter", "impose_power_tick_request", 0x1A23C094, 0);
        }
    }
    const uint32_t rate = rp_u32(c, gp + 0x6DC);
    if ((int32_t)rate > 0) {
        const uint32_t numerator = (rate >> 16) & 0x7FFF, denominator = rate & 0xFFFF;
        rp_w32(c, gp + 0x35E0, denominator - numerator);
        if (rp_u32(c, gp + 0x35D4))
            rp_block(c, "display_timer_unlink_not_reconstructed", 0x9668);
        if (!denominator) rp_block(c, "display_rate_zero_divisor", 0x115B4);
        schedule_event(c, gp + 0x35D0, (0x89D00 / denominator) * numerator);
    }
    rp_event(c, "milestone", "first_disabled_display_command_sequence", start, (out - start) / 4);
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
    raise_irq(c, 1);
    rp_event(c, "milestone", "first_guest_frame_event_scheduled", 0x1265C, (uint32_t)rounded);
    if ((uint32_t)half(c, c->gp + 0x3610) + (uint32_t)half(c, c->gp + 0x3612) * 0x400 ==
            rp_u32(c, c->gp + 0x35C8) || (rp_u32(c, c->gp + 0x6AC) & 0x20000000)) {
        if (!(mode & 0x800) || !(rp_u32(c, c->gp + 0x6AC) & 8)) {
            rp_w8(c, c->gp + 0x3667, 1);
            refresh_disabled_display(c);
        }
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
    gpu_control_reset(c);
    const uint32_t output = rp_u32(c, c->gp + 0x362C);
    static const uint32_t commands[] = {0xD4000000,0xD507FFFF,0x55000000,0x13041B90,0x0A000040};
    for (unsigned i = 0; i < 5; ++i) rp_w32(c, output + i * 4, commands[i]);
    rp_w32(c, c->gp + 0x362C, output + 20);
    rp_event(c, "milestone", "graphics_command_buffer_initialized", output, 5);
    begin_frame(c);
    rp_block(c, "post_frame_graphics_initialization_not_reconstructed", 0x1BA98);
}
