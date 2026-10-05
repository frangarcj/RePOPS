#include "../src/bootstrap.h"
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf exit_target;
static struct {
    int32_t create_result, start_result, exit_error;
    uint32_t base;
    char events[16];
    unsigned count;
} state;

static void event(char value)
{
    assert(state.count + 1 < sizeof(state.events));
    state.events[state.count++] = value;
}

static int32_t sdk(void *ctx, uint32_t value)
{
    assert(ctx == &state && value == UINT32_C(0x06060010));
    event('S');
    return -1; /* Its return is deliberately ignored by the original. */
}
static int32_t compiler(void *ctx, uint32_t value)
{
    assert(ctx == &state && value == UINT32_C(0x30306));
    event('C');
    return -2;
}
static int32_t hold(void *ctx, int32_t value)
{
    assert(ctx == &state && value == 1);
    event('H');
    return -3;
}
static int32_t create(void *ctx, const char *name, uint32_t entry,
                      uint32_t priority, uint32_t stack, uint32_t flags, uint32_t opt)
{
    assert(ctx == &state && strcmp(name, "popsmain") == 0);
    assert(entry == state.base + UINT32_C(0x16080));
    assert(priority == 0x20 && stack == UINT32_C(0xbed900));
    assert(flags == UINT32_C(0x80104000) && opt == 0);
    event('T');
    return state.create_result;
}
static int32_t start(void *ctx, int32_t thread, uint32_t size, uint32_t arg)
{
    assert(ctx == &state && thread == state.create_result && size == 0 && arg == 0);
    event('R');
    return state.start_result;
}
static void exit_vsh(void *ctx, int32_t error)
{
    assert(ctx == &state);
    state.exit_error = error;
    event('E');
    longjmp(exit_target, 1);
}

int main(void)
{
    static const int32_t values[] = {INT32_MIN, -1, 0, 1, 42, INT32_MAX};
    const repops_host host = {&state, sdk, compiler, hold, create, start, exit_vsh};
    unsigned cases = 0;
    for (unsigned a = 0; a < sizeof(values)/sizeof(values[0]); ++a) {
        for (unsigned b = 0; b < sizeof(values)/sizeof(values[0]); ++b) {
            memset(&state, 0, sizeof(state));
            state.create_result = values[a];
            state.start_result = values[b];
            state.base = UINT32_C(0x08800000);
            if (setjmp(exit_target) == 0) {
                int32_t result = repops_module_start_model(&host, state.base);
                assert(state.start_result >= 0 && result == 0);
                assert(strcmp(state.events, "SCHTR") == 0);
            } else {
                assert(state.start_result < 0 && state.exit_error == state.start_result);
                assert(strcmp(state.events, "SCHTRE") == 0);
            }
            cases++;
        }
    }
    printf("Bootstrap model: %u contract cases passed. Not binary-equivalence validation.\n", cases);
    return 0;
}
