#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define HEADER UINT32_C(0x09E80000)
#define HEADER_BYTES UINT32_C(0xB3C80)

static void word(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static rp_context *fixture(size_t trim)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->trace = tmpfile();
    c->disc = tmpfile();
    assert(c->trace && c->disc);
    c->regions[0] = (rp_region){0, 0x800000, calloc(1, 0x800000)};
    c->regions[2] = (rp_region){HEADER, 0xC0000, calloc(1, 0xC0000)};
    assert(c->regions[0].bytes && c->regions[2].bytes);
    c->gp = 0x10000;
    c->psar_offset = 64;
    c->data_psp_word = UINT32_C(0x464C457F);
    c->disc_bytes = 64 + HEADER_BYTES - trim;
    uint8_t *file = calloc(1, 64 + HEADER_BYTES);
    assert(file);
    uint8_t *h = file + 64;
    memcpy(h, "PSISOIMG0000", 12);
    memcpy(h + 0x400, "_TEST_00001", 12);
    word(h + 0x1220, UINT32_C(0xFFFFFFF0));
    word(h + 0x1224, 3);
    word(h + 0x12B4, 0x40);
    word(h + 0x12B8, 0x24);
    assert(fwrite(file, 1, (size_t)c->disc_bytes, c->disc) == c->disc_bytes);
    assert(fflush(c->disc) == 0);
    memcpy(rp_memory(c, HEADER, 0x400), h, 0x400);
    free(file);
    return c;
}

static void dispose(rp_context *c)
{
    fclose(c->trace);
    fclose(c->disc);
    free(c->regions[0].bytes);
    free(c->regions[2].bytes);
    free(c);
}

static void expect_header_block(rp_context *c, uint32_t header, uint32_t disc, const char *kind)
{
    if (setjmp(c->stop) == 0) {
        (void)rp_provider_plain_disc_header(c, header, disc);
        assert(!"Expected an explicit format/bounds blocker");
    }
    assert(strcmp(c->stop_kind, kind) == 0);
    assert(c->disc_header.valid == 0);
}

int main(void)
{
    rp_context *c = fixture(0);
    const uint32_t expected = UINT32_C(0x464C457F) ^ UINT32_C(0x4A08B53F);
    assert(rp_provider_plain_disc_header(c, HEADER, 0) == expected);
    assert(c->disc_header.valid == 1);
    assert(c->disc_header.auxiliary_offset == 0x80);
    assert(c->disc_header.auxiliary_size == 0x24);
    assert(c->disc_header.offset_1220 == UINT64_C(0x400000030));
    assert(strcmp(rp_memory(c, HEADER + 0x400, 32), "_TEST_00001") == 0);
    assert(rp_pops_normalize_disc_id(c, HEADER + 0x400, 32) == HEADER + 0x400);
    assert(strcmp(rp_memory(c, HEADER + 0x400, 32), "TEST00001") == 0);
    assert(rp_pops_check_disc_id(c, HEADER + 0x400) == 0);
    assert(rp_pops_remember_provider_result(c, expected) == expected);
    assert(rp_u32(c, 0x49CBE0) == (expected ^ UINT32_C(0x9136B780)));
    assert(rp_pops_remember_provider_result(c, UINT32_MAX) == UINT32_MAX);
    assert(rp_u32(c, 0x49CBE0) == (expected ^ UINT32_C(0x9136B780)));
    dispose(c);

    c = fixture(1);
    assert(rp_provider_plain_disc_header(c, HEADER, 0) == UINT32_MAX);
    assert(!c->disc_header.valid);
    dispose(c);

    c = fixture(0);
    c->data_psp_word = UINT32_C(0x5053507E);
    expect_header_block(c, HEADER, 0, "protected_or_unknown_pbp_provider");
    dispose(c);

    c = fixture(0);
    expect_header_block(c, HEADER, 1, "multidisc_provider_not_reconstructed");
    dispose(c);

    c = fixture(0);
    expect_header_block(c, UINT32_C(0xFFFFF000), 0, "disc_header_address_overflow");
    dispose(c);

    c = fixture(0);
    assert(fseek(c->disc, 64 + 0x400, SEEK_SET) == 0);
    assert(fwrite("\0PGD", 1, 4, c->disc) == 4);
    assert(fflush(c->disc) == 0);
    expect_header_block(c, HEADER, 0, "protected_or_unknown_disc_header");
    dispose(c);

    c = fixture(0);
    rp_w8(c, HEADER, 0);
    assert(rp_provider_plain_disc_header(c, HEADER, 0) == UINT32_MAX);
    assert(!c->disc_header.valid);
    dispose(c);

    c = fixture(0);
    char *id = rp_memory(c, HEADER + 0x400, 32);
    strcpy(id, "TEST-00001");
    assert(rp_pops_normalize_disc_id(c, HEADER + 0x400, 32) == HEADER + 0x400);
    assert(rp_pops_check_disc_id(c, HEADER + 0x400) == 0);
    id[0] = 't';
    assert(rp_pops_check_disc_id(c, HEADER + 0x400) == UINT32_MAX);
    id[0] = 'T'; id[7] = 'X';
    assert(rp_pops_check_disc_id(c, HEADER + 0x400) == UINT32_MAX);
    strcpy(id, "__TEST");
    assert(rp_pops_normalize_disc_id(c, HEADER + 0x400, 0) == HEADER + 0x400);
    assert(strcmp(id, "__TEST") == 0);
    memset(id, '_', 32);
    if (setjmp(c->stop) == 0) {
        (void)rp_pops_normalize_disc_id(c, HEADER + 0x400, 32);
        assert(!"Expected an unterminated-field blocker");
    }
    assert(strcmp(c->stop_kind, "unterminated_disc_id") == 0);
    dispose(c);
    puts("Native disc header: synthetic read/state/format/ID tests passed; no protected-file or ME emulation.");
    return 0;
}
