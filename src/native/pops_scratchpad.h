#ifndef REPOPS_POPS_SCRATCHPAD_H
#define REPOPS_POPS_SCRATCHPAD_H
#include "pops_state.h"

typedef struct {
    uint8_t earlier_state[0x3000];
    uint8_t ps1_scratchpad[0x400];
} rp_core_scratchpad_layout;
#define RP_PS1_SCRATCHPAD(c) RP_FIELD_ADDRESS((c)->gp, rp_core_scratchpad_layout, ps1_scratchpad)
enum {
    RP_STORE_SCRATCH_BYTE = 0x1DB4,
    RP_STORE_SCRATCH_HALF = 0x20F4,
    RP_STORE_SCRATCH_WORD = 0x2434,
};

/* Returns false without mutation outside the recovered scratchpad and warm
 * RAM paths. Other memory regions remain with the existing dispatcher. */
bool rp_pops_scratchpad_store(rp_context *c);
_Static_assert(offsetof(rp_core_scratchpad_layout, ps1_scratchpad) == 0x3000, "PS1 scratchpad backing");
#endif
