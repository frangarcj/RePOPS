#include "runtime.h"
#include <string.h>

enum { MC_GROUP_BASE = 0x10CB50, MC_GROUP_SIZE = 0x20088,
       MC_DIRECTORY_BASE = 0x4A0C24, MC_DIRECTORY_SIZE = 0x2018,
       MC_RAW_POINTERS = 0x4A2C24, MC_CARD_BYTES = 0x20000 };

static void check_slot(rp_context *c, uint32_t slot)
{
    if (slot >= 2) rp_block(c, "memory_card_slot_not_supported", slot);
}
static uint32_t raw_card(rp_context *c, uint32_t slot)
{
    check_slot(c, slot);
    return rp_u32(c, MC_RAW_POINTERS + slot * MC_DIRECTORY_SIZE);
}

/* +0x37204 with bank zero, then +0x37B8C. Raw card bytes follow the
 * registered slot's 0x80-byte container header.
 */
static void bind_slot(rp_context *c, uint32_t slot, uint32_t buffer)
{
    rp_function(c, 0x37204, "pops.bind_memory_card_slot_bank_zero");
    rp_w32(c, 0x4514EC + slot * 4, buffer);
    rp_function(c, 0x37B8C, "pops.bind_raw_memory_card");
    rp_w32(c, MC_RAW_POINTERS + slot * MC_DIRECTORY_SIZE, buffer + 0x80);
    rp_w32(c, 0x450EA0 + slot * 4, buffer);
}

/* +0x39740: XOR only the first 127 bytes. */
static uint8_t card_checksum(rp_context *c, uint32_t address)
{
    rp_function(c, 0x39740, "pops.memory_card_sector_checksum");
    const uint8_t *p = rp_memory(c, address, 128);
    uint8_t sum = 0;
    for (unsigned i = 0; i < 127; ++i) sum ^= p[i];
    return sum;
}

/* +0x399EC's successful in-memory path. Update both cached directory and
 * backing sectors. Serialization helpers collapse to byte copies because
 * their layout is little-endian; the runtime stores all guest words as LE.
 */
void rp_pops_mc_format(rp_context *c, uint32_t slot)
{
    const uint32_t raw = raw_card(c, slot), directory = MC_DIRECTORY_BASE + slot * MC_DIRECTORY_SIZE;
    rp_function(c, 0x399EC, "pops.format_memory_card_in_memory");
    if (rp_u32(c, 0x49CBC8)) rp_block(c, "memory_card_library_busy", 0x396B8);
    memset(rp_memory(c, directory, 0x2000), 0, 0x2000);
    rp_w8(c, directory + 127, 0x0E);
    rp_w8(c, directory, 'M'); rp_w8(c, directory + 1, 'C');
    memcpy(rp_memory(c, raw, 128), rp_memory(c, directory, 128), 128);
    for (unsigned i = 0; i < 15; ++i) {
        const uint32_t row = directory + (i + 1) * 128;
        rp_w32(c, row, 0xA0);
        rp_w8(c, row + 8, 0xFF); rp_w8(c, row + 9, 0xFF);
        rp_w8(c, row + 126, 0);
        rp_w8(c, row + 127, card_checksum(c, row));
        memcpy(rp_memory(c, raw + (i + 1) * 128, 128), rp_memory(c, row, 128), 128);
    }
    for (unsigned i = 0; i < 20; ++i) {
        const uint32_t row = directory + 0x800 + i * 128;
        rp_w32(c, row, UINT32_MAX); rp_w32(c, row + 8, 0xFFFF);
        rp_w8(c, row + 127, card_checksum(c, row));
        memcpy(rp_memory(c, raw + 0x800 + i * 128, 128), rp_memory(c, row, 128), 128);
    }
}

/* A fresh volatile savedata backend, not a successful VMP read/signature
 * check. No user file is opened, created or overwritten by this backend.
 */
static void load_volatile_card(rp_context *c, uint32_t slot)
{
    if (!c->diagnostic_skip_ui)
        rp_block(c, "persistent_memory_card_backend_not_selected", 0x37814);
    ++c->services;
    rp_event(c, "volatile_storage_adapter", "new_card_in_RAM_no_VMP_or_savedata_IO", slot, MC_CARD_BYTES);
    rp_pops_mc_format(c, slot);
}

/* +0x37BB8. */
static void repair_empty_slots(rp_context *c, uint32_t slot)
{
    rp_function(c, 0x37BB8, "pops.normalize_memory_card_broken_sectors");
    const uint32_t raw = raw_card(c, slot);
    for (unsigned i = 0; i < 20; ++i) {
        const uint32_t row = raw + 0x800 + i * 128;
        if (!rp_u32(c, row) && !rp_u32(c, row + 8)) {
            rp_w32(c, row + 8, 0xFFFF); rp_w32(c, row, UINT32_MAX);
        }
    }
}

/* +0x3A390. Preserve the original OR-style 'M'/'C' admission check.
 * Entries whose high nibble is 5 are allocated, regardless of low nibble.
 */
uint32_t rp_pops_mc_free_blocks(rp_context *c, uint32_t slot, uint32_t *free_blocks)
{
    check_slot(c, slot);
    rp_function(c, 0x3A390, "pops.memory_card_free_blocks");
    const uint32_t directory = MC_DIRECTORY_BASE + slot * MC_DIRECTORY_SIZE;
    const uint8_t *header = rp_memory(c, directory, 2);
    *free_blocks = 0;
    if (header[0] != 'M' && header[1] != 'C') return 0x8101002F;
    for (unsigned i = 0; i < 15; ++i)
        if ((rp_u32(c, directory + (i + 1) * 128) & 0xF0) != 0x50) ++*free_blocks;
    return 0;
}

/* +0x1AA90 startup and park point. File/container and savedata utility calls
 * use the fresh-card backend. Dirty-card writeback remains a separate task.
 */
void rp_pops_mc_worker_start(rp_context *c)
{
    if (c->mc_worker_ready) return;
    if (c->mc_thread_entry != 0x1AA90)
        rp_block(c, "memory_card_worker_not_queued", 0x1AA90);
    rp_function(c, 0x1AA90, "pops.memory_card_worker_startup");
    memset(rp_memory(c, MC_GROUP_BASE, 0x40110), 0, 0x40110);
    if (rp_u32(c, 0x14D07C)) rp_block(c, "memory_card_restore_state_not_reconstructed", 0x1C970);
    uint32_t all_empty = 1;
    for (unsigned slot = 0; slot < 2; ++slot) {
        const uint32_t group = MC_GROUP_BASE + slot * MC_GROUP_SIZE;
        bind_slot(c, slot, group + 8);
        load_volatile_card(c, slot);
        repair_empty_slots(c, slot);
        /* Fresh-card directory data already came from the formatter; this
         * does not claim to parse a VMP or implement +0x37D68's whole call tree.
         */
        uint32_t free_blocks;
        const uint32_t result = rp_pops_mc_free_blocks(c, slot, &free_blocks);
        all_empty &= result == 0 && free_blocks == 15;
        rp_w8(c, group, *(uint8_t *)rp_memory(c, group, 1) | 1);
        rp_w8(c, group + 5, 0);
        rp_event(c, "milestone", "volatile_memory_card_initialized", slot, free_blocks);
    }
    if (all_empty) {
        rp_function(c, 0x2870C, "pops.update_empty_cards_notification");
        rp_function(c, 0x371B4, "pops.query_savedata_notification_enabled");
        rp_w32(c, 0x14D094, (rp_u32(c, 0x450EBC) >> 31) ^ 1);
    }
    rp_function(c, 0x27EA8, "pops.mark_memory_cards_ready");
    rp_w32(c, 0x14D0F0, 1);
    rp_w32(c, 0x14CC64, UINT32_MAX);
    c->mc_worker_ready = 1;
    rp_event(c, "milestone", "mcworker_startup_signal_written_then_wait_semaphore", 0x14CC64, UINT32_MAX);
}
