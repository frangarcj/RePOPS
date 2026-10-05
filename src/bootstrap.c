#include "bootstrap.h"
#include <stdlib.h>

/* Reconstructed host-call model, not a buildable POPS emulator.
 * Evidence: original instructions at +0x16000..+0x1607f and the no-return
 * POPSMAN exit contract. Register/stack effects are outside this model.
 */
int32_t repops_module_start_model(const repops_host *host, uint32_t module_base)
{
    void *ctx = host->context;
    (void)host->set_sdk_version(ctx, UINT32_C(0x06060010));
    (void)host->set_compiler_version(ctx, UINT32_C(0x00030306));
    (void)host->set_display_hold(ctx, 1);

    int32_t thread = host->create_thread(ctx, "popsmain",
        module_base + UINT32_C(0x00016080), UINT32_C(0x20),
        UINT32_C(0x00bed900), UINT32_C(0x80104000), 0);

    /* The original does NOT branch on create_thread's return value. */
    int32_t result = host->start_thread(ctx, thread, 0, 0);
    if (result >= 0)
        return 0;

    host->exit_vsh(ctx, result);
    /* Returning from exit_vsh violates the model's host precondition. */
    abort();
}
