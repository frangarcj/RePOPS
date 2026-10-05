#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile();
    c->gp = 0x10000;
    c->regions[2] = (rp_region){0x09B80000, 0x1000, calloc(1, 0x1000)};
    assert(c->trace && c->regions[2].bytes);
    /* Synthetic words, not firmware: taken branch, delay-slot store, signed
     * extension, bitwise FPR transfer, then a native-helper call boundary.
     */
    const uint32_t words[] = {
        0x2402FFFF, 0x18400002, 0xAF820020, 0x24020063,
        0x4482A000, 0x4403A000, 0x0C002268, 0xAF830024
    };
    for (unsigned i = 0; i < sizeof(words)/sizeof(words[0]); ++i)
        rp_w32(c, 0x09B80000 + i * 4, words[i]);
    rp_w32(c, c->gp + 0x1D0, 0x09B80020);
    c->run_pc = 0x09B80000; c->run_next_pc = c->run_pc + 4;
    c->run_gpr[28] = c->gp;
    if (setjmp(c->stop)) { fprintf(stderr, "%s\n", c->stop_kind); return 1; }
    for (unsigned i = 0; i < 7; ++i) rp_generated_step(c);
    assert(c->run_pc == 0x89A0 && c->run_gpr[31] == 0x09B80020);
    assert(c->run_fpr[20] == UINT32_MAX && c->run_gpr[2] == UINT32_MAX);
    assert(rp_u32(c, c->gp + 0x20) == UINT32_MAX);
    assert(rp_u32(c, c->gp + 0x24) == UINT32_MAX);
    assert(c->generated_instructions == 7 && c->run_gpr[0] == 0);
    fclose(c->trace); free(c->regions[2].bytes); free(c);
    puts("Generated-code adapter: branch/call delay slots, signed value and FPR bits passed.");
    return 0;
}
