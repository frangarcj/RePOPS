#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile(); c->gp = 0x10000;
    c->regions[0] = (rp_region){0, 0x100000, calloc(1, 0x100000)};
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
    assert(c->generated_cache_invalidations == 1);
    assert(rp_u32(c, c->gp + 0x20) == 42 && c->run_fpr[20] == 42);
    c->run_fpr[20] = 153;
    c->run_pc = c->run_gpr[31];
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x24) == 153);
    const char *legacy = getenv("REPOPS_UNICORN_ALWAYS_FLUSH");
    const bool always_flush = legacy && legacy[0] == '1';
    assert(c->generated_cache_invalidations == (always_flush ? 2u : 1u));
    /* A native cache patch must invalidate the previously translated fragment. */
    rp_w32(c, start + 16, 0x24030007);
    c->run_pc = start + 16;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x24) == 7);
    /* A RAM-derived block lives in the second generated-code cache, not in
     * executable PS1 RAM. Its call can cross back to the BIOS-derived cache.
     */
    const uint32_t ram = 0x09540000;
    rp_w32(c, ram, 0x24020019);
    rp_w32(c, ram + 4, 0x0C000000 | ((start + 16) >> 2));
    rp_w32(c, ram + 8, 0xAF820028);
    rp_w32(c, c->gp + 0x1CC, ram + 12);
    c->run_pc = ram;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x28) == 25);
    assert(rp_u32(c, c->gp + 0x24) == 7);
    /* +0x7E60 rewinds allocation while +0x1A68 can still return into the
     * unchanged old block. Do not confuse that PC with a firmware helper. */
    rp_w32(c, c->gp + 0x1CC, ram);
    assert(rp_generated_known_address(c, ram + 4));
    assert(!rp_generated_known_address(c, ram + 12));
    assert(!rp_generated_known_address(c, 0x1A68));
    c->run_pc = ram;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x28) == 25);
    assert(rp_u32(c, c->gp + 0x1CC) == ram);
    /* Reuse of the old address still executes the new bytes, not a cached
     * translation made valid forever by the retained executable extent. */
    rp_w32(c, ram, 0x2402001F);
    c->run_pc = ram;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x28) == 31);
    /* Same callsite: a RAM halfword write, then an I/O address. The latter
     * must return to C instead of corrupting a RAM alias. */
    const uint32_t probe = start + 0x100;
    rp_w32(c, probe, 0x0C000000 | (RP_FAST_RAM_SH >> 2));
    rp_w32(c, probe + 4, 0);
    rp_w32(c, probe + 8, 0x0C000000 | (0x2888 >> 2));
    rp_w32(c, probe + 12, 0);
    rp_w32(c, c->gp + 0x1D0, probe + 16);
    c->run_pc = probe; c->run_gpr[4] = 0x80000120; c->run_gpr[5] = 0xBEEF;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2888 && rp_u32(c, 0x09800120) == 0xBEEF);
    c->run_pc = probe; c->run_gpr[4] = 0x1F801104;
    rp_unicorn_run(c);
    assert(c->run_pc == 0x2110 && c->run_gpr[4] == 0x1F801104);
    /* Repeated RAM reads stay in the engine, but the same callsite must exit
     * to the right C helper when its address changes to I/O. */
    const uint32_t readers[] = {RP_FAST_RAM_LH, RP_FAST_RAM_LHU};
    const uint32_t fallbacks[] = {0x1DE8, 0x267C};
    const uint32_t values[] = {UINT32_C(0xFFFFBEEF), 0xBEEF};
    for (unsigned i = 0; i < 2; ++i) {
        rp_w32(c, probe, 0x0C000000 | (readers[i] >> 2));
        c->run_pc = probe; c->run_gpr[4] = 0x80000120;
        rp_unicorn_run(c);
        assert(c->run_pc == 0x2888 && c->run_gpr[2] == values[i]);
        c->run_pc = probe; c->run_gpr[4] = 0x1F801DAE;
        rp_unicorn_run(c);
        assert(c->run_pc == fallbacks[i] && c->run_gpr[4] == 0x1F801DAE);
    }
    /* A pointer obtained through the uncached alias can patch cached code. */
    uint8_t *patch = rp_memory(c, (start + 16) | 0x40000000, 4);
    patch[0] = 9;
    c->run_pc = start + 16;
    rp_unicorn_run(c);
    assert(rp_u32(c, c->gp + 0x24) == 9);
    /* Writes performed by generated instructions also dirty translations. */
    const uint32_t self = start + 0x200;
    const uint32_t self_patch[] = {0x3C0409B8, 0x34840010, 0x3C052403, 0x34A5000B,
                                  0xAC850000, 0x0C000A22, 0};
    for (unsigned i = 0; i < sizeof(self_patch) / sizeof(self_patch[0]); ++i)
        rp_w32(c, self + i * 4, self_patch[i]);
    rp_w32(c, c->gp + 0x1D0, self + sizeof(self_patch));
    c->run_pc = self;
    rp_unicorn_run(c);
    const uint64_t flushes = c->generated_cache_invalidations;
    c->run_pc = start + 16;
    rp_unicorn_run(c);
    assert(c->generated_cache_invalidations == flushes + 1);
    assert(rp_u32(c, c->gp + 0x24) == 11);
    /* The generated FLAG transfer is Allegrex VFPU, not MIPS32 COP2.
     * Bridge its bits before Unicorn raises an exception; do not patch it. */
    const uint32_t scalar_words[] = {0x4868000F,0x48E9000F,0x486A000F,0x0C000A22,0};
    const uint32_t scalar_starts[] = {start + 0x300, ram + 0x100};
    for (unsigned cache = 0; cache < 2; ++cache) {
        const uint32_t scalar = scalar_starts[cache];
        for (unsigned i = 0; i < sizeof(scalar_words) / sizeof(scalar_words[0]); ++i)
            rp_w32(c, scalar + i * 4, scalar_words[i]);
        rp_w32(c, c->gp + (cache ? 0x1CC : 0x1D0), scalar + sizeof(scalar_words));
        c->vfpu_s330_bits = 0x81234567;
        c->run_gpr[9] = 0x7FC01234;
        c->run_pc = scalar;
        rp_unicorn_run(c);
        assert(c->run_pc == scalar + 4 && c->run_gpr[8] == 0x81234567);
        rp_unicorn_run(c);
        assert(c->run_pc == scalar + 8 && c->vfpu_s330_bits == 0x7FC01234);
        rp_unicorn_run(c);
        assert(c->run_pc == scalar + 12 && c->run_gpr[10] == 0x7FC01234);
        rp_unicorn_run(c);
        assert(c->run_pc == 0x2888);
        for (unsigned i = 0; i < sizeof(scalar_words) / sizeof(scalar_words[0]); ++i)
            assert(rp_u32(c, scalar + i * 4) == scalar_words[i]);
    }
    const char *iterations_text = getenv("REPOPS_BENCH_REENTRIES");
    const unsigned iterations = iterations_text ? (unsigned)strtoul(iterations_text, NULL, 10) : 0;
    const uint64_t before = c->generated_cache_invalidations;
    for (unsigned i = 0; i < iterations; ++i) {
        c->run_pc = start + 16;
        rp_unicorn_run(c);
        assert(c->run_pc == 0x2888 && rp_u32(c, c->gp + 0x24) == 11);
    }
    assert(c->generated_cache_invalidations - before == (always_flush ? iterations : 0));
    if (iterations) printf("Reentries=%u invalidations=%llu\n", iterations,
                          (unsigned long long)(c->generated_cache_invalidations - before));
    const uint32_t delayed = start + 0x380;
    rp_w32(c, delayed, 0x08000000 | ((delayed + 16) >> 2));
    rp_w32(c, delayed + 4, 0x4868000F);
    rp_w32(c, c->gp + 0x1D0, delayed + 20);
    c->run_pc = delayed;
    if (setjmp(c->stop) == 0) {
        rp_unicorn_run(c);
        assert(!"VFPU delay slot silently lost its branch");
    }
    assert(c->stop_address == delayed + 4);
    assert(strcmp(c->stop_kind, "unicorn_S330_transfer_in_delay_slot_not_supported") == 0);
    rp_unicorn_close(c);
    fclose(c->trace); free(c->regions[0].bytes); free(c->regions[2].bytes); free(c);
    puts("Unicorn cache: exits, reuse, patches and S330 bit transfers; unsupported delay slot refused.");
    return 0;
}
