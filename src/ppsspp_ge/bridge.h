#ifndef REPOPS_PPSSPP_GE_BRIDGE_H
#define REPOPS_PPSSPP_GE_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One process-local software GE. These functions never step a PSP CPU. */
int rp_ppsspp_ge_open(void);
void rp_ppsspp_ge_close(void);
void *rp_ppsspp_ge_memory(uint32_t address, size_t bytes);
int rp_ppsspp_ge_enqueue(uint32_t start, uint32_t stall);
int rp_ppsspp_ge_stall(int id, uint32_t stall);
int rp_ppsspp_ge_sync(int id);
const char *rp_ppsspp_ge_error(void);
#ifdef __cplusplus
}
#endif
#endif
