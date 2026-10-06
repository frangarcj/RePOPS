#ifndef REPOPS_TIMER_H
#define REPOPS_TIMER_H
#include "pops_cdrom.h"

/* Layout already established by +0x9B6C/+0x9A54 and timer initialization.
 * Guest-cycle origins and event pointers are u32 values, not host pointers. */
typedef struct {
    rp_guest_event_layout event;
    uint32_t target_with_flags, origin_cycles, mode_with_status;
    uint8_t irq_bit, clock_shift, unknown_1e[2];
} rp_timer_layout;
typedef struct {
    uint8_t earlier_000[0x64C];
    rp_timer_layout counters[3];
} rp_core_timer_layout;

#define RP_TIMER_BASE(c, channel) RP_FIELD_ADDRESS((c)->gp, rp_core_timer_layout, counters[channel])
#define RP_TIMER_FIELD(base, member) RP_FIELD_ADDRESS(base, rp_timer_layout, member)
uint32_t rp_pops_timer_read(rp_context *, uint32_t address, uint32_t width);

_Static_assert(sizeof(rp_timer_layout) == 0x20, "timer stride");
_Static_assert(offsetof(rp_timer_layout, target_with_flags) == 0x10, "timer target");
_Static_assert(offsetof(rp_timer_layout, clock_shift) == 0x1D, "timer clock shift");
_Static_assert(offsetof(rp_core_timer_layout, counters) == 0x64C, "core timer base");
#endif
