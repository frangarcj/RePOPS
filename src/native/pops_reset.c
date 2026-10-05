#include "runtime.h"
#include "../me_startup.h"
#include <string.h>
#include <time.h>

static void halfword(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

/* These native fields belong to the provider, not POPS's base-zero data.
 * The pre-registration control path returns without waiting for an ACK in
 * the original too. Codec output is an explicit headless host adaptation.
 */
static uint32_t me_read(void *ctx, uint32_t address)
{
    rp_context *c = ctx;
    if (address == 0xBFC007F0) return c->me_ack;
    if (address == 0x4C5C) return c->me_callback;
    rp_block(c, "unimplemented_native_me_read", address);
}
static void me_write(void *ctx, uint32_t address, uint32_t value)
{
    rp_context *c = ctx;
    if (address != 0xBFC007F8) rp_block(c, "unimplemented_native_me_write", address);
    c->me_request = value;
    rp_event(c, "reconstructed_provider", "me_control_request", address, value);
}
static uint32_t me_service(void *ctx, uint32_t service, uint32_t a0, uint32_t a1, uint32_t a2)
{
    rp_context *c = ctx; (void)a1; (void)a2;
    if (service == REPOPS_ME_DELAY) {
        rp_event(c, "host_adapter", "cooperative_ME_poll_without_wall_clock_delay", service, a0);
        return 0;
    }
    if (service != REPOPS_ME_CODEC_376399B6)
        rp_block(c, "unimplemented_native_me_service", service);
    ++c->services;
    rp_event(c, "headless_adapter", "codec_control_without_audio_device", service, a0);
    return 0;
}
static void me_control(rp_context *c, uint32_t request)
{
    const repops_me_startup_host host = {c, me_read, me_write, me_service};
    repops_me_wait wait;
    repops_me_control_begin(&host, request, &wait);
    if (repops_me_wait_step(&host, &wait)) return;
    for (unsigned step = 0; step < 128; ++step) {
        rp_pops_me_poll(c);
        if (repops_me_wait_step(&host, &wait)) return;
    }
    rp_block(c, "native_me_awaiting_real_ack", 0x3514);
}

/* +0x30B84. Provider value/control effects run; host clock scaling is logged. */
static uint32_t set_run_mode(rp_context *c, uint32_t mode)
{
    rp_function(c, 0x30B84, "pops.set_run_mode");
    const uint32_t old = rp_u32(c, 0x14D0F4);
    rp_w32(c, 0x14D0F4, mode);
    c->me_value = (rp_u32(c, 0x163238) * UINT32_C(0x4000) + 0x8000) >> 5;
    rp_event(c, "reconstructed_provider", "me_value_shift", 0xBFC007F4, c->me_value);
    if (old != mode) {
        if (mode == 0) {
            if (old != 1) return 0;
        } else {
            me_control(c, 1);
            if (mode & 2) return mode;
        }
        ++c->services;
        rp_event(c, "headless_adapter", "clock_scaling_not_applied", 0x469989AD,
                 mode == 0 ? 333 : 166);
    }
    return mode;
}
static void update_me_control(rp_context *c)
{
    rp_function(c, 0x25190, "pops.update_me_control");
    me_control(c, rp_u32(c, 0x14D0F4) != 0 || rp_u32(c, 0x14D09C) != 0);
}

/* +0x1A950 queues a separate memory-card worker; it does not execute its body. */
uint32_t rp_pops_mc_init(rp_context *c)
{
    rp_function(c, 0x1A950, "pops.memory_card_worker_init");
    const uint32_t semaphore = ++c->next_id;
    rp_w32(c, 0x14CC60, semaphore);
    c->mc_semaphore_count = 0;
    c->mc_thread_entry = 0x1AA90;
    const uint32_t thread = ++c->next_id;
    c->services += 3;
    rp_event(c, "host_adapter", "create_mc_writeback_semaphore", semaphore, 0);
    rp_event(c, "deferred_worker", "memory_card_worker_body_not_reconstructed", c->mc_thread_entry, thread);
    return thread;
}

/* +0x1A57C: state initialization is native; controller services are headless. */
uint32_t rp_pops_controller_init(rp_context *c)
{
    rp_function(c, 0x1A57C, "pops.controller_state_init");
    c->services += 3;
    rp_event(c, "headless_adapter", "controller_sampling_mode", 0, 1);
    rp_event(c, "headless_adapter", "controller_idle_thresholds", 64, 64);
    memset(rp_memory(c, c->gp + 0x3C00, 0x60), 0, 0x60);
    for (unsigned port = 0; port < 2; ++port) {
        const uint32_t base = c->gp + port * 0x30;
        rp_w8(c, base + 0x3C21, 1);
        rp_w8(c, base + 0x3C24, 0x41);
        rp_w8(c, base + 0x3C2C, 0x5A);
        rp_w8(c, base + 0x3C22, 0x41);
        rp_w8(c, base + 0x3C23, 2);
        rp_w32(c, base + 0x3C00, UINT32_MAX);
    }
    rp_event(c, "headless_adapter", "controller_sampling_cycle", 0,
             rp_u32(c, c->gp + 0x6AC) & 0x100000 ? 0x2095 : 0);
    return 0;
}

/* +0x1A314: integer table, including MIN, MSUB and EXT semantics.
 * The emitted table remains guest data; it is not executed by this harness.
 */
static void reciprocal_table(rp_context *c)
{
    rp_function(c, 0x1A314, "pops.reciprocal_table_init");
    uint32_t destination = 0x097E0000;
    for (uint32_t n = 0x8000; n < 0x10000; ++n, destination -= 4) {
        uint32_t index = ((n + 0x40) >> 7) - 0x100;
        if (index > 0xFF) index = 0xFF;
        const uint32_t factor = (0x40100 + index) / (2 * index + 0x200);
        const uint32_t residual = 0x80 - factor * n;
        const uint32_t middle = (residual >> 8) & 0x1FFFF;
        const uint32_t result = ((middle * factor + 0x80) >> 8) << 14;
        rp_w32(c, destination, result);
    }
    rp_event(c, "milestone", "reciprocal_table_prepared", 0x097C0004, 32768);
}

/* +0x1C55C: keep the initialized VFPU rows as host data, not machine code.
 * R403 is zero and is used by the original vector memory-fill loops.
 */
static void vector_constants(rp_context *c)
{
    rp_function(c, 0x1C55C, "pops.vector_reset_constants");
    const uint32_t bits = 0x40FFFE00;
    float last; memcpy(&last, &bits, sizeof(last));
    const float rows[4][4] = {
        {256.0f, 32.0f, -8.0f, last}, {128.0f, 16.0f, -8.0f, last},
        {64.0f, 8.0f, -8.0f, last}, {0.0f, 0.0f, 0.0f, 0.0f}
    };
    memcpy(c->vfpu_reset_rows, rows, sizeof(rows));
    c->vfpu_s330_bits = rp_u32(c, c->gp + 0xFC);
    c->vfpu_zero_ready = 1;
}
static void cpu_reset(rp_context *c)
{
    rp_function(c, 0x1A2A4, "pops.cpu_reset_state");
    rp_w32(c, c->gp + 0x130, 0x400000);
    rp_w32(c, c->gp + 0x13C, 1);
    rp_w32(c, c->gp + 0x7C, 0x20);
    rp_w32(c, c->gp + 0x1A0, 0xBFC00000);
    vector_constants(c);
    rp_event(c, "milestone", "psx_reset_state_prepared_not_executed", c->gp + 0x1A0, 0xBFC00000);
}

/* +0x1A050. Write original numeric handler entries to the guest table. Having
 * these addresses in memory does not mean the corresponding C handlers exist.
 */
static void map_io(rp_context *c, uint32_t address, uint32_t length, uint32_t reader, uint32_t writer)
{
    rp_function(c, 0x1A050, "pops.install_io_handler_entries");
    if (!reader) reader = 0x8A54;
    if (!writer) writer = 0x8AA4;
    if (length < 8 || length > 0x1000)
        rp_block(c, "io_mapping_length_not_supported", address);
    const uint32_t start = (address & ~UINT32_C(7)) - 0x1F7F0000;
    for (uint32_t i = 0; i < (length >> 3); ++i) {
        rp_w32(c, start + i * 8, reader);
        rp_w32(c, start + i * 8 + 4, writer);
    }
}
static void io_reset(rp_context *c)
{
    rp_function(c, 0x1A0A8, "pops.io_map_reset");
    const uint32_t value = rp_u32(c, c->gp + 0x71C) + 1;
    for (uint32_t i = 0; i < 0x400; i += 4) rp_w32(c, c->gp + 0x3000 + i, value);
    map_io(c, 0x1F801000, 0x1000, 0x88BC, 0x89A0);
    map_io(c, 0x1F801060, 0x10, 0, 0);
    map_io(c, 0x1F801000, 0x40, 0, 0);
}
static void dma_reset(rp_context *c)
{
    rp_function(c, 0x1A134, "pops.dma_reset");
    memset(rp_memory(c, c->gp + 0x1E8, 0xC8), 0, 0xC8);
    halfword(c, c->gp + 0x2AA, 6);
    rp_w32(c, c->gp + 0x29C, 0x8B1C);
    rp_w32(c, c->gp + 0x20F0, 0x77777777);
    rp_w32(c, c->gp + 0x2A0, 0x9364);
    map_io(c, 0x1F8010E0, 8, 0x9158, 0);
    map_io(c, 0x1F8010E8, 8, 0x9158, 0x92A4);
    map_io(c, 0x1F8010F0, 8, 0, 0x91BC);
}
static void dma_channel(rp_context *c, uint32_t channel, uint32_t handler)
{
    rp_function(c, 0x1A1F0, "pops.install_dma_channel");
    channel &= 0xFF;
    const uint32_t offset = channel * 0x1C;
    rp_w32(c, c->gp + 0x1F8 + offset, handler);
    rp_w32(c, c->gp + 0x1F4 + offset, 0x8B1C);
    halfword(c, c->gp + 0x202 + offset, (uint16_t)channel);
    map_io(c, 0x1F801080 + channel * 16, 8, 0x9158, 0);
    map_io(c, 0x1F801088 + channel * 16, 8, 0x9158, 0x92A4);
}
static void mdec_reset(rp_context *c)
{
    rp_function(c, 0x1B678, "pops.mdec_reset_entries");
    dma_channel(c, 0, 0xF54C);
    dma_channel(c, 1, 0xF654);
    map_io(c, 0x1F801820, 8, 0xF6DC, 0xF70C);
    memcpy(rp_memory(c, c->gp + 0x400, 0x1C), rp_module_memory(c, 0xD499C, 0x1C), 0x1C);
    rp_w32(c, c->gp + 0x648, 0xE8F8);
}
static void irq_reset(rp_context *c)
{
    rp_function(c, 0x1A2E0, "pops.interrupt_handler_entries");
    map_io(c, 0x1F801070, 8, 0x9850, 0x98C4);
}
static void disc_state_reset(rp_context *c)
{
    rp_function(c, 0x1AF38, "pops.disc_state_reset");
    const uint8_t old = *(uint8_t *)rp_memory(c, c->gp + 0x388D, 1);
    memset(rp_memory(c, c->gp + 0x3800, 0xC0), 0, 0xC0);
    rp_w8(c, c->gp + 0x388D, old);
    rp_w8(c, c->gp + 0x38B8, 0x80);
    rp_w8(c, c->gp + 0x3880, 1);
    rp_w8(c, c->gp + 0x38BA, 0x80);
    const uint32_t auxiliary = rp_u32(c, 0x49CBD0);
    if (auxiliary) rp_w32(c, c->gp + 0x1DC, auxiliary + 12);
}

static void serial_reset(rp_context *c)
{
    rp_function(c, 0x1A494, "pops.serial_port_reset");
    halfword(c, c->gp + 0x2EC, 0x195);
    rp_w32(c, c->gp + 0x2F8, 0x1A56C);
    rp_w8(c, c->gp + 0x2C7, 1);
    rp_w8(c, c->gp + 0x2F3, 2);
    halfword(c, c->gp + 0x2C0, 5);
    rp_w32(c, c->gp + 0x2CC, 0x9E64);
    map_io(c, 0x1F801040, 8, 0x9F30, 0xA06C);
    map_io(c, 0x1F801050, 8, 0x9F30, 0xA06C);
    map_io(c, 0x1F801048, 8, 0, 0xA0E8);
    map_io(c, 0x1F801058, 8, 0, 0xA0E8);
}
static void code_cache_reset(rp_context *c)
{
    rp_function(c, 0x19F10, "pops.code_cache_reset");
    uint32_t mode = rp_u32(c, c->gp + 0x6FC);
    if (mode & 0x80000000) mode = (rp_u32(c, c->gp + 0x6B0) >> 1) & 1;
    rp_w8(c, c->gp + 0x1C2, (uint8_t)mode);
    rp_w32(c, c->gp + 0x1CC, 0x09540000);
    rp_w32(c, c->gp + 0x1D0, 0x09B80000);
    rp_w32(c, c->gp + 0x3CF8, 0);
    if (!c->vfpu_zero_ready) rp_block(c, "vector_fill_source_not_initialized", 0x19F10);
    /* VWB.Q R403 writes the zero row prepared at +0x1C55C. */
    memset(rp_memory(c, 0x09C00000, 0x280000), 0, 0x280000);
}
static void cd_audio_sync(rp_context *c)
{
    rp_function(c, 0xD9F4, "pops.cd_audio_sync_prefix");
    if (*(uint8_t *)rp_memory(c, c->gp + 0x3E46, 1))
        rp_block(c, "pending_cd_audio_read_not_reconstructed", 0xD9F4);
    rp_function(c, 0x11520, "pops.audio_pacing_no_delay_path");
    const uint32_t elapsed = rp_u32(c, c->gp + 0x1AC) - rp_u32(c, c->gp + 0x1B0)
                             - rp_u32(c, c->gp + 0x360C);
    const uint32_t produced = rp_u32(c, 0x49F40294) - rp_u32(c, c->gp + 0x3608);
    const uint32_t pending = elapsed - produced * UINT32_C(0x300);
    if ((int32_t)pending > 0xD3A)
        rp_block(c, "audio_pacing_delay_path_not_reconstructed", 0x11520);
    rp_w8(c, 0x49F40293, 0xFF);
}
static void cd_controller_reset(rp_context *c)
{
    rp_function(c, 0x1ADB0, "pops.cd_controller_reset");
    const uint8_t mode = *(uint8_t *)rp_memory(c, c->gp + 0x3880, 1);
    const uint8_t saved = *(uint8_t *)rp_memory(c, c->gp + 0x388F, 1);
    rp_w32(c, c->gp + 0x38B0, 0);
    if (rp_u32(c, c->gp + 0x384C)) rp_block(c, "active_cd_timer_unlink", 0x9668);
    rp_w8(c, c->gp + 0x389D, *(uint8_t *)rp_memory(c, c->gp + 0x389D, 1) & 0x17);
    const uint32_t links[] = {0x3820, 0x3804, 0x383C, 0x385C};
    for (unsigned i = 0; i < 4; ++i)
        if (rp_u32(c, c->gp + links[i])) rp_block(c, "active_cd_timer_unlink", 0x9668);
    const uint8_t retained = *(uint8_t *)rp_memory(c, c->gp + 0x388D, 1);
    memset(rp_memory(c, c->gp + 0x3800, 0xB8), 0, 0xB8);
    rp_w8(c, c->gp + 0x388F, saved);
    rp_w8(c, c->gp + 0x38BC, *(uint8_t *)rp_memory(c, c->gp + 0x38BC, 1) & 0xDF);
    rp_w8(c, c->gp + 0x388D, retained);
    rp_w32(c, c->gp + 0x380C, 0xC268);
    rp_w32(c, c->gp + 0x3828, 0xC268);
    rp_w32(c, c->gp + 0x3864, 0xCE00);
    rp_w8(c, c->gp + 0x389C, 0x20);
    halfword(c, c->gp + 0x387A, 12);
    rp_w8(c, c->gp + 0x389D, 2);
    rp_w8(c, c->gp + 0x3880, mode);
    halfword(c, c->gp + 0x387C, 0x930);
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) rp_block(c, "host_clock_failed", 0x1ADB0);
    const uint32_t micros = (uint32_t)((uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000);
    ++c->services;
    rp_event(c, "host_adapter", "system_time_low_monotonic", 0x1ADB0, micros);
    rp_w32(c, c->gp + 0x38A0, micros);
    rp_w8(c, c->gp + 0x387E, 0xFF);
    cd_audio_sync(c);
    rp_w32(c, 0x49F4028C, rp_u32(c, c->gp + 0x38B8));
    dma_channel(c, 3, 0xCE18);
    map_io(c, 0x1F801800, 0x10, 0xD088, 0xD1B0);
}
static void gpu_status_reset(rp_context *c)
{
    rp_function(c, 0x1A284, "pops.gpu_status_reset");
    rp_w32(c, c->gp + 0x1AC, 0x204CC00);
    rp_w32(c, c->gp + 0x1BC, c->gp + 0x1B8);
    rp_w32(c, c->gp + 0x1B0, 0x204CC00);
    rp_w32(c, c->gp + 0x1B8, c->gp + 0x1B8);
}
static void timers_reset(rp_context *c)
{
    rp_function(c, 0x1A3A8, "pops.timer_handler_entries");
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t timer = c->gp + 0x64C + i * 0x20;
        rp_w8(c, timer + 0x1C, (uint8_t)(0x10 << i));
        rp_w8(c, timer + 0x1D, 0);
        rp_w32(c, timer + 0x10, 0x10000);
        rp_w32(c, timer + 0xC, 0x9AD0);
        map_io(c, 0x1F801100 + i * 16, 8, 0x9BE0, 0x9C60);
        map_io(c, 0x1F801108 + i * 16, 8, 0, 0x9C60);
    }
}
static void spu_state_reset(rp_context *c)
{
    rp_function(c, 0x19DD8, "pops.spu_state_and_tables_reset");
    memset(rp_memory(c, 0x09FF0000, 0x179C), 0, 0x179C);
    rp_w8(c, 0x09FF178E, 0x80);
    halfword(c, 0x09FF178C, 1);
    const uint8_t *coefficients = rp_module_memory(c, 0xD3C20, 0x400);
    for (unsigned i = 0; i < 128; ++i) {
        memcpy(rp_memory(c, 0x09FF0000 + i * 8, 8), coefficients + i * 8, 8);
        uint8_t *reverse = rp_memory(c, 0x09FF07F8 - i * 8, 8);
        for (unsigned j = 0; j < 4; ++j)
            memcpy(reverse + j * 2, coefficients + i * 8 + (3 - j) * 2, 2);
    }
    memcpy(rp_memory(c, 0x09FF0846, 10), rp_module_memory(c, 0xD4020, 10), 10);
    halfword(c, 0x09FF0850, 0x7FFF);
    halfword(c, 0x09FF0854, 0x6000);
    memcpy(rp_memory(c, 0x09FF0800, 0x46), rp_module_memory(c, 0xD402C, 0x46), 0x46);
    memset(rp_memory(c, 0x09F40000, 0x802C0), 0, 0x802C0);
    const uint32_t config = rp_u32(c, c->gp + 0x700);
    const uint32_t flags = rp_u32(c, c->gp + 0x6AC);
    rp_w8(c, 0x09FF1795, (uint8_t)((config >> 8) - config));
    rp_w32(c, 0x09F40288, 0x80000000);
    rp_w8(c, 0x09FF178F, (uint8_t)((flags >> 8) & 1));
    rp_w8(c, 0x09FF1796, (uint8_t)((flags >> 7) & 1));
    rp_w8(c, 0x09FF1794, (uint8_t)config);
    rp_event(c, "host_adapter", "shared_RAM_cache_barrier_elided", 0x19DD8, 0);
}
static void sound_reset(rp_context *c)
{
    rp_function(c, 0x19F8C, "pops.sound_reset_and_me_start");
    spu_state_reset(c);
    memset(rp_memory(c, c->gp + 0x308, 0x78), 0, 0x78);
    rp_w32(c, c->gp + 0x35C, 0x8580);
    rp_w32(c, c->gp + 0x36C, 0x8898);
    dma_channel(c, 4, 0x8698);
    map_io(c, 0x1F801C00, 0x260, 0x85F4, 0);
    map_io(c, 0x1F801C00, 0x200, 0x85F4, 0x7F00);
    rp_function(c, 0x25184, "pops.get_volume_setting");
    c->me_value = (rp_u32(c, 0x163238) * UINT32_C(0x4000) + 0x8000) >> 5;
    rp_event(c, "milestone", "spu_state_prepared_before_me_callback", 0x09F40000, 0x802C0);
    rp_pops_start_me(c);
}

void rp_pops_initialize_core(rp_context *c)
{
    rp_function(c, 0x24C78, "pops.initialize_core_prefix");
    reciprocal_table(c);
    rp_function(c, 0x24B58, "pops.reset_devices_prefix");
    memset(rp_memory(c, c->gp, 0x6AC), 0, 0x6AC);
    memset(rp_memory(c, 0x09FFA000, 0x1000), 0, 0x1000);
    (void)set_run_mode(c, 3);
    update_me_control(c);
    cpu_reset(c);
    io_reset(c);
    dma_reset(c);
    mdec_reset(c);
    irq_reset(c);
    disc_state_reset(c);
    serial_reset(c);
    code_cache_reset(c);
    cd_controller_reset(c);
    gpu_status_reset(c);
    timers_reset(c);
    rp_event(c, "milestone", "initial_device_handler_tables_prepared", 0x11000, 0x1000);
    sound_reset(c);
    (void)set_run_mode(c, 0);
    update_me_control(c);
    rp_event(c, "milestone", "native_ME_start_and_resume_complete", 0xBFC007F0, c->me_ack);
    rp_block(c, "function_not_reconstructed", 0x1B9C4);
}
