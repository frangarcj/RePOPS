#ifndef REPOPS_NATIVE_RUNTIME_H
#define REPOPS_NATIVE_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include "me_worker.h"

#define RP_REGION_COUNT 4
/* Execution-adapter addresses, not original POPS entry points. */
enum {
    RP_FAST_BIOS_LBU = 0x07000000, RP_FAST_RAM_SB = 0x07000040,
    RP_FAST_RAM_LBU = 0x07000080, RP_FAST_RAM_LW = 0x070000C0,
    RP_FAST_RAM_SW = 0x07000100, RP_FAST_RAM_SH = 0x07000140
};
typedef struct rp_region { uint32_t base, size; uint8_t *bytes; } rp_region;
/* Recovered fields of the single-disc provider path, not a POPSMAN RAM image.
 * No PSP hardware mode is guessed when reading a header on the host.
 */
typedef struct rp_disc_header_state {
    uint64_t offset_1220; /* Semantic use is still unresolved. */
    uint32_t auxiliary_offset, auxiliary_size, valid;
} rp_disc_header_state;
typedef struct rp_context {
    rp_region regions[RP_REGION_COUNT];
    uint8_t scratchpad[0x4000];
    FILE *trace, *disc;
    const char *disc_path;
    uint32_t gp, heap_next, next_id, thread_entry, psar_offset;
    uint32_t data_psp_word;
    uint32_t cd_thread_entry, cd_event_bits;
    uint32_t mc_thread_entry, mc_semaphore_count, mc_worker_ready;
    uint32_t me_callback, me_request, me_ack, me_value;
    uint32_t me_stack_hi, me_stack_lo, me_output_words, me_last_output;
    uint32_t ge_commands[512], ge_command_count, ge_lists_captured;
    uint32_t ge_stalled_list, ge_edram_translation;
    /* Temporary execution adapter for code emitted by the reconstructed C.
     * Original firmware instructions are never fetched by this adapter.
     */
    uint32_t run_gpr[32], run_fpr[32], run_pc, run_next_pc, run_hi, run_lo;
    uint64_t generated_instructions;
    uint32_t compiled_transfers;
    void *generated_engine;
    const char *generated_executor;
    rp_me_worker me_worker;
    uint32_t vfpu_s330_bits;
    float vfpu_reset_rows[4][4];
    int diagnostic_skip_ui, vfpu_zero_ready;
    uint64_t disc_bytes;
    rp_disc_header_state disc_header;
    uint32_t functions, services, imports, last_function;
    jmp_buf stop;
    const char *stop_kind;
    uint32_t stop_address;
} rp_context;

void rp_event(rp_context *, const char *kind, const char *name, uint32_t address, uint32_t value);
_Noreturn void rp_block(rp_context *, const char *kind, uint32_t address);
void *rp_memory(rp_context *, uint32_t address, size_t length);
void *rp_module_memory(rp_context *, uint32_t offset, size_t length);
uint32_t rp_module_u32(rp_context *, uint32_t offset);
uint32_t rp_u32(rp_context *, uint32_t);
void rp_w32(rp_context *, uint32_t, uint32_t);
void rp_w8(rp_context *, uint32_t, uint8_t);
const char *rp_string(rp_context *, uint32_t);
uint32_t rp_alloc(rp_context *, uint32_t);
void rp_function(rp_context *, uint32_t, const char *);
void rp_pops_map_io(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
void rp_pops_install_dma(rp_context *, uint32_t, uint32_t);
void rp_pops_prepare_exception(rp_context *, uint32_t);
int32_t rp_provider_open_image(rp_context *, uint32_t);
int32_t rp_provider_read_at(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_provider_plain_disc_header(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_remember_provider_result(rp_context *, uint32_t);
uint32_t rp_pops_normalize_disc_id(rp_context *, uint32_t, int32_t);
uint32_t rp_pops_check_disc_id(rp_context *, uint32_t);
uint32_t rp_pops_apply_game_config(rp_context *, uint32_t, uint32_t, uint32_t, uint32_t);
void rp_pops_config_postprocess(rp_context *);
int32_t rp_pops_msf_to_sector(rp_context *, uint32_t);
uint32_t rp_pops_finalize_disc_selection(rp_context *, uint32_t, uint32_t);
int32_t rp_pops_module_start(rp_context *);
void rp_pops_main_thread(rp_context *);
uint32_t rp_pops_patch_syscalls(rp_context *);
uint32_t rp_pops_disc_init(rp_context *);
uint32_t rp_pops_icon_info(rp_context *, uint32_t, uint32_t);
void rp_pops_icon_tag(rp_context *, uint32_t);
void rp_pops_savedata_metadata(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_halfword_length(rp_context *, uint32_t, int32_t);
void rp_pops_optional_metadata(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_optional_auxiliary(rp_context *, uint32_t);
void rp_pops_finish_disc_boot(rp_context *);
uint32_t rp_pops_mc_init(rp_context *);
void rp_pops_mc_worker_start(rp_context *);
void rp_pops_mc_format(rp_context *, uint32_t);
uint32_t rp_pops_mc_free_blocks(rp_context *, uint32_t, uint32_t *);
uint32_t rp_pops_controller_init(rp_context *);
void rp_pops_initialize_core(rp_context *);
void rp_pops_graphics_initialize(rp_context *);
void rp_pops_graphics_event(rp_context *, uint32_t);
uint32_t rp_pops_dispatch_events(rp_context *);
void rp_pops_irq_write(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_irq_read(rp_context *, uint32_t);
void rp_pops_timer_write(rp_context *, uint32_t, uint32_t);
void rp_pops_dma_control_write(rp_context *, uint32_t, uint32_t, uint32_t);
void rp_pops_schedule_event(rp_context *, uint32_t, uint32_t);
void rp_pops_remove_event(rp_context *, uint32_t);
void rp_pops_spu_write_register(rp_context *, uint32_t, uint32_t, uint32_t);
uint32_t rp_pops_spu_read_register(rp_context *, uint32_t, uint32_t);
void rp_pops_start_me(rp_context *);
void rp_pops_me_poll(rp_context *);
bool rp_pops_spu_inactive_sample(rp_context *, uint32_t *);
bool rp_pops_spu_sample(rp_context *, uint32_t *);
void rp_pops_analyze_records(rp_context *, uint32_t);
uint32_t rp_pops_prepare_compile(rp_context *, uint32_t);
void rp_generated_step(rp_context *);
void rp_unicorn_open(rp_context *);
void rp_unicorn_run(rp_context *);
void rp_unicorn_close(rp_context *);
void rp_pops_run_core(rp_context *);
void rp_pops_default_write(rp_context *, uint32_t, uint32_t, uint32_t);
void rp_pops_invalidate_ram_code(rp_context *);
#endif
