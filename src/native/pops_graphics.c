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

/* +0x11410 up to its first display refresh. ROUND.W.S uses nearest/even. */
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
            rp_block(c, "function_not_reconstructed", 0x115B4);
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
