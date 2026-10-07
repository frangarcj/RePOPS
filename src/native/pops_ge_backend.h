#ifndef REPOPS_GE_BACKEND_H
#define REPOPS_GE_BACKEND_H
#include "runtime.h"

#ifdef REPOPS_WITH_PPSSPP_GE
int rp_ge_live_open(rp_context *c);
void rp_ge_live_close(rp_context *c);
uint32_t rp_ge_live_enqueue(rp_context *c, uint32_t start, uint32_t stall);
void rp_ge_live_stall(rp_context *c, uint32_t id, uint32_t stall);
void rp_ge_live_sync(rp_context *c, uint32_t id);
uint32_t rp_ge_live_state(rp_context *c, const uint32_t *words, uint32_t count);
#else
static inline int rp_ge_live_open(rp_context *c) { (void)c; return 0; }
static inline void rp_ge_live_close(rp_context *c) { (void)c; }
static inline uint32_t rp_ge_live_enqueue(rp_context *c, uint32_t start, uint32_t stall) {
    (void)stall; rp_block(c, "GE_backend_not_built", start);
}
static inline void rp_ge_live_stall(rp_context *c, uint32_t id, uint32_t stall) {
    (void)id; rp_block(c, "GE_backend_not_built", stall);
}
static inline void rp_ge_live_sync(rp_context *c, uint32_t id) {
    rp_block(c, "GE_backend_not_built", id);
}
static inline uint32_t rp_ge_live_state(rp_context *c, const uint32_t *words, uint32_t count) {
    (void)words; rp_block(c, "GE_backend_not_built", count);
}
#endif
#endif
