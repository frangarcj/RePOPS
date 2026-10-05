#ifndef REPOPS_NATIVE_RUNTIME_H
#define REPOPS_NATIVE_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>

typedef struct rp_region { uint32_t base, size; uint8_t *bytes; } rp_region;
typedef struct rp_context {
    rp_region regions[3];
    uint8_t scratchpad[0x4000];
    FILE *trace, *disc;
    const char *disc_path;
    uint32_t gp, heap_next, next_id, thread_entry, psar_offset;
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
int32_t rp_pops_module_start(rp_context *);
void rp_pops_main_thread(rp_context *);
uint32_t rp_pops_patch_syscalls(rp_context *);
uint32_t rp_pops_disc_init(rp_context *);
uint32_t rp_pops_icon_info(rp_context *, uint32_t, uint32_t);
void rp_pops_icon_tag(rp_context *, uint32_t);
#endif
