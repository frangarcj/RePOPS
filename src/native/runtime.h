#ifndef REPOPS_NATIVE_RUNTIME_H
#define REPOPS_NATIVE_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>

#define RP_REGION_COUNT 4
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
    uint32_t mc_thread_entry, mc_semaphore_count;
    uint32_t me_callback, me_request, me_ack, me_value;
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
uint32_t rp_pops_controller_init(rp_context *);
void rp_pops_initialize_core(rp_context *);
#endif
