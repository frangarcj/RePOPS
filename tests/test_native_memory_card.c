#include "../src/native/pops_memory_card.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void serial_header(rp_context *c, unsigned slot, unsigned command, unsigned sector)
{
    assert(rp_pops_mc_serial(c, 0, slot, 0x81) == 0xFF);
    assert(rp_pops_mc_serial(c, 1, slot, command) == 0);
    assert(rp_pops_mc_serial(c, 2, slot, 0) == 0x5A);
    assert(rp_pops_mc_serial(c, 3, slot, 0) == 0x5D);
    assert(rp_pops_mc_serial(c, 4, slot, sector >> 8) == 0);
    assert(rp_pops_mc_serial(c, 5, slot, sector & 0xFF) == 0);
}

static void check_serial_protocol(rp_context *c)
{
    for (unsigned slot = 0; slot < 2; ++slot) {
        const unsigned sector = slot ? 1023 : 17;
        uint8_t expected_xor = (uint8_t)((sector >> 8) ^ sector);
        for (unsigned i = 0; i < 128; ++i)
            rp_w8(c, RP_MC_SLOT(slot, sectors[sector][i]), (uint8_t)(i * 13 + slot));
        serial_header(c, slot, 0x52, sector);
        assert(rp_pops_mc_serial(c, 6, slot, 0) == 0x5C);
        assert(rp_pops_mc_serial(c, 7, slot, 0) == 0x5D);
        assert(rp_pops_mc_serial(c, 8, slot, 0) == sector >> 8);
        assert(rp_pops_mc_serial(c, 9, slot, 0) == (sector & 0xFF));
        for (unsigned i = 0; i < 128; ++i) {
            const uint8_t expected = (uint8_t)(i * 13 + slot);
            assert(rp_pops_mc_serial(c, 10 + i, slot, 0) == expected);
            expected_xor ^= expected;
        }
        assert(rp_pops_mc_serial(c, 138, slot, 0) == expected_xor);
        assert(rp_pops_mc_serial(c, 139, slot, 0) == 0x147);
        assert(rp_cd_u8(c, RP_MC_SLOT(slot, flags)) == 1);
    }
    serial_header(c, 0, 0x52, 1024);
    assert(rp_pops_mc_serial(c, 6, 0, 0) == 0x1FF);
    rp_w8(c, RP_MC_SLOT(1, flags), 0);
    assert(rp_pops_mc_serial(c, 0, 1, 0x81) == 0x1FF);
    rp_w8(c, RP_MC_SLOT(1, flags), 1);

    /* Bad-checksum writes still alter RAM; sector 0x3f writes do not.
     * A valid ordinary write stops before an unreconstructed save alarm. */
    const unsigned sectors[] = {7, 0x3F, 9};
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const unsigned sector = sectors[attempt];
        uint8_t checksum = (uint8_t)sector;
        memset(rp_memory(c, RP_MC_SLOT(0, sectors[sector]), 128), 0xA5, 128);
        serial_header(c, 0, 0x57, sector);
        for (unsigned i = 0; i < 128; ++i) {
            const uint8_t value = (uint8_t)(i ^ 0x69);
            assert(rp_pops_mc_serial(c, 6 + i, 0, value) == 0xFF);
            checksum ^= value;
            assert(rp_cd_u8(c, RP_MC_SLOT(0, sectors[sector][i])) ==
                   (sector == 0x3F ? 0xA5 : value));
        }
        assert(rp_pops_mc_serial(c, 134, 0, checksum ^ (attempt == 0)) == 0xFF);
        assert(rp_pops_mc_serial(c, 135, 0, 0) == 0x5C);
        assert(rp_pops_mc_serial(c, 136, 0, 0) == 0x5D);
        if (attempt < 2) {
            assert(rp_pops_mc_serial(c, 137, 0, 0) == (attempt == 0 ? 0x14E : 0x147));
            assert(rp_cd_u8(c, RP_MC_SLOT(0, flags)) == 1);
        } else {
            if (!setjmp(c->stop)) {
                (void)rp_pops_mc_serial(c, 137, 0, 0);
                assert(!"Save alarm was falsely acknowledged");
            }
            assert(!strcmp(c->stop_kind, "memory_card_save_alarm_not_reconstructed"));
            assert(rp_cd_u8(c, RP_MC_SLOT(0, flags)) == 3);
        }
    }
}

int main(void)
{
    rp_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->regions[0] = (rp_region){0, 0x800000, calloc(1, 0x800000)};
    c->trace = tmpfile();
    assert(c->regions[0].bytes && c->trace);
    c->gp = 0x10000;
    c->diagnostic_skip_ui = 1;
    c->mc_thread_entry = 0x1AA90;
    rp_w32(c, 0x450EBC, UINT32_MAX);
    if (setjmp(c->stop)) {
        fprintf(stderr, "Unexpected blocker: %s\n", c->stop_kind);
        return 1;
    }
    rp_pops_mc_worker_start(c);
    assert(c->mc_worker_ready && rp_u32(c, 0x14CC64) == UINT32_MAX);
    assert(rp_u32(c, 0x14D0F0) == 1 && rp_u32(c, 0x14D094) == 0);
    for (unsigned slot = 0; slot < 2; ++slot) {
        const uint32_t group = 0x10CB50 + slot * 0x20088;
        const uint32_t raw = rp_u32(c, 0x4A2C24 + slot * 0x2018);
        assert(raw == group + 0x88);
        assert(*(uint8_t *)rp_memory(c, group, 1) == 1);
        assert(!memcmp(rp_memory(c, raw, 2), "MC", 2));
        for (unsigned sector = 0; sector < 36; ++sector) {
            const uint8_t *bytes = rp_memory(c, raw + sector * 128, 128);
            uint8_t sum = 0;
            for (unsigned j = 0; j < 128; ++j) sum ^= bytes[j];
            assert(sum == 0);
            if (sector >= 1 && sector <= 15) assert(bytes[0] == 0xA0);
            if (sector >= 16) assert(rp_u32(c, raw + sector * 128) == UINT32_MAX);
        }
        uint32_t free_blocks;
        assert(rp_pops_mc_free_blocks(c, slot, &free_blocks) == 0 && free_blocks == 15);
    }
    const uint32_t calls = c->functions;
    rp_pops_mc_worker_start(c);
    assert(c->functions == calls); /* A parked worker is not reinitialized. */
    rp_w32(c, 0x4A0CA4, 0x51);
    uint32_t free_blocks;
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0 && free_blocks == 14);
    rp_w8(c, 0x4A0C24, 'X');
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0); /* Original OR check. */
    rp_w8(c, 0x4A0C25, 'X');
    assert(rp_pops_mc_free_blocks(c, 0, &free_blocks) == 0x8101002F && free_blocks == 0);
    check_serial_protocol(c);
    fclose(c->trace); free(c->regions[0].bytes); free(c);
    puts("Memory card: startup plus full serial reads, checksum/write ordering and save boundary passed.");
    return 0;
}
