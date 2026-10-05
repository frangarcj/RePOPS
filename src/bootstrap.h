#ifndef REPOPS_BOOTSTRAP_H
#define REPOPS_BOOTSTRAP_H

#include <stdint.h>

/* Analysis model of the host-visible calls at POPS 6.60 +0x16000.
 * Guest addresses remain numbers. They are NOT callable host pointers.
 * All callbacks must be supplied; exit_vsh must never return.
 */
typedef struct repops_host {
    void *context;
    int32_t (*set_sdk_version)(void *, uint32_t);
    int32_t (*set_compiler_version)(void *, uint32_t);
    int32_t (*set_display_hold)(void *, int32_t);
    int32_t (*create_thread)(void *, const char *, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t);
    int32_t (*start_thread)(void *, int32_t, uint32_t, uint32_t);
    void (*exit_vsh)(void *, int32_t);
} repops_host;

int32_t repops_module_start_model(const repops_host *host, uint32_t module_base);

#endif
