#include "../native/pops_ge_backend.h"
#include "bridge.h"
#include <stdlib.h>
#include <string.h>

static void checked(rp_context *c, int result, uint32_t address) {
    if (result < 0) {
        fprintf(stderr, "PPSSPP GE: %s\n", rp_ppsspp_ge_error());
        rp_block(c, "PPSSPP_GE_execution_failed", address);
    }
}
static void scratch_in(rp_context *c) {
    void *scratch = rp_ppsspp_ge_memory(0x10000, sizeof(c->scratchpad));
    if (!scratch) { checked(c, -1, 0x10000); return; }
    memcpy(scratch, c->scratchpad, sizeof(c->scratchpad));
}
static void scratch_out(rp_context *c) {
    memcpy(c->scratchpad, rp_ppsspp_ge_memory(0x10000, sizeof(c->scratchpad)), sizeof(c->scratchpad));
}

int rp_ge_live_open(rp_context *c) {
    if (rp_ppsspp_ge_open() < 0) {
        fprintf(stderr, "PPSSPP GE startup: %s\n", rp_ppsspp_ge_error());
        return -1;
    }
    void *backing[RP_REGION_COUNT] = {0};
    for (unsigned i = 2; i < RP_REGION_COUNT; ++i) {
        backing[i] = rp_ppsspp_ge_memory(c->regions[i].base, c->regions[i].size);
        if (!backing[i]) { rp_ppsspp_ge_close(); return -1; }
    }
    for (unsigned i = 2; i < RP_REGION_COUNT; ++i) {
        memcpy(backing[i], c->regions[i].bytes, c->regions[i].size);
        free(c->regions[i].bytes);
        c->regions[i].bytes = backing[i];
    }
    c->ge_backend_active = 1;
    rp_event(c, "GE_backend", "PPSSPP_software_shared_RAM_and_4MiB_EDRAM", 0x04000000, 0x400000);
    return 0;
}
void rp_ge_live_close(rp_context *c) {
    if (!c->ge_backend_active) return;
    rp_ppsspp_ge_close();
    c->ge_backend_active = 0;
}
int rp_ge_live_dump_edram(rp_context *c, const char *path) {
    if (!c->ge_backend_active) return -1;
    FILE *file = fopen(path, "wbx");
    if (!file) { perror("GE EDRAM snapshot"); return -1; }
    const size_t bytes = fwrite(c->regions[3].bytes, 1, c->regions[3].size, file);
    const int closed = fclose(file);
    if (bytes != c->regions[3].size || closed) {
        fprintf(stderr, "Incomplete GE EDRAM snapshot\n");
        return -1;
    }
    rp_event(c, "GE_backend", "actual_EDRAM_snapshot_written", c->regions[3].base, (uint32_t)bytes);
    return 0;
}
uint32_t rp_ge_live_enqueue(rp_context *c, uint32_t start, uint32_t stall) {
    scratch_in(c);
    const int id = rp_ppsspp_ge_enqueue(start, stall);
    scratch_out(c);
    checked(c, id, start);
    ++c->ge_backend_submissions;
    rp_event(c, "GE_backend", "list_enqueued", start, (uint32_t)id + 1);
    return (uint32_t)id + 1; /* Preserve POPS zero-id sentinel. */
}
void rp_ge_live_stall(rp_context *c, uint32_t id, uint32_t stall) {
    if (!id) rp_block(c, "PPSSPP_GE_missing_list", stall);
    scratch_in(c);
    const int result = rp_ppsspp_ge_stall((int)id - 1, stall);
    scratch_out(c);
    checked(c, result, stall);
    rp_event(c, "GE_backend", "stall_processed_and_drawing_flushed", id, stall);
}
void rp_ge_live_sync(rp_context *c, uint32_t id) {
    if (!id) rp_block(c, "PPSSPP_GE_missing_list", id);
    scratch_in(c);
    const int result = rp_ppsspp_ge_sync((int)id - 1);
    scratch_out(c);
    checked(c, result, id);
    ++c->ge_backend_completed;
    rp_event(c, "GE_backend", "completed_list_pixels_available", id, c->ge_backend_completed);
}
uint32_t rp_ge_live_state(rp_context *c, const uint32_t *words, uint32_t count) {
    const uint32_t staging = 0x0BEF0000;
    void *target = rp_ppsspp_ge_memory(staging, count * 4);
    if (!target) checked(c, -1, staging);
    memcpy(target, words, count * 4);
    const uint32_t id = rp_ge_live_enqueue(c, staging, 0);
    rp_ge_live_sync(c, id);
    return id;
}
