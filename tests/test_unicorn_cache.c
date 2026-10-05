#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); c->gp = 0x10000;
    c->regions[0] = (rp_region){0, 0x20000, calloc(1, 0x20000)};
    c->regions[2] = (rp_region){0x08000000, 0x2000000, calloc(1, 0x2000000)};
    assert(c->trace && c->regions[0].bytes && c->regions[2].bytes);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected Unicorn stop: %s at %08X\n", c->stop_kind, c->stop_address);
        return 1;
    }
    const uint32_t start = 0x09B80000;
    const uint32_t words[] = {
        0x2402002A, 0x4482A000, 0x0C002268, 0xAF820020,
        0x4403A000, 0x0C000A22, 0xAF830024
    };
    for (unsigned i = 0; i < sizeof(words)/sizeof(words[0]); ++i)
        rp_w32(c, start + i * 4, words[i]);
    rp_w32(c, c->gp + 0x1D0, start + sizeof(words));
    c->run_pc = start; c->run_gpr[28] = c->gp;
    rp_unicorn_open(c);
    rp_unicorn_run(c);
    assert(c->run_pc == 0x89A0 && c->run_gpr[31] == start + 16);
    assert(rp_u32(c, c->gp + 0x20) == 42 && c->run_fpr[20] == 42);
    c->run_fpr[20] = 153;
    c->run_pc = c->run_gpr[31];
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x24) == 153);
    /* A native cache patch must invalidate the previously translated fragment. */
    rp_w32(c, start + 16, 0x24030007);
    c->run_pc = start + 16;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x24) == 7);
    rp_unicorn_close(c);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[2].bytes); free(c);
    puts("Unicorn cache: helper exits, delay slot, shared memory, FPR bits and native patch passed.");
    return 0;
}
