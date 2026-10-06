#include "pops_cdrom.h"
#include "pops_dma.h"
#include <string.h>
#include <math.h>

#define CD8(member) rp_cd_u8(c, RP_CD_ADDRESS(c, member))
#define CD16(member) rp_cd_u16(c, RP_CD_ADDRESS(c, member))
#define CD32(member) rp_u32(c, RP_CD_ADDRESS(c, member))
#define SET8(member, value) rp_w8(c, RP_CD_ADDRESS(c, member), (uint8_t)(value))
#define SET16(member, value) rp_cd_w16(c, RP_CD_ADDRESS(c, member), (uint16_t)(value))
#define SET32(member, value) rp_w32(c, RP_CD_ADDRESS(c, member), (uint32_t)(value))

static void command(rp_context *c, unsigned opcode);

/* Setmode immediately reinserts these nodes. Unlike the general remove helper,
 * its inline unlink does not adjust the core deadline/downcount. */
static void reschedule_mode_event(rp_context *c, uint32_t event, uint32_t delay)
{
    const uint32_t next = rp_u32(c, RP_FIELD_ADDRESS(event, rp_guest_event_layout, next));
    const uint32_t prev = rp_u32(c, RP_FIELD_ADDRESS(event, rp_guest_event_layout, prev));
    rp_w32(c, RP_FIELD_ADDRESS(next, rp_guest_event_layout, prev), prev);
    rp_w32(c, RP_FIELD_ADDRESS(prev, rp_guest_event_layout, next), next);
    rp_pops_schedule_event(c, event, delay);
}

/* +0xC480: the long-seek term multiplies truncated SQRT.S by 6000.
 * Raw decompiler pointer arithmetic obscures that factor as 1500. */
uint32_t rp_pops_cd_seek_cycles(rp_context *c, uint32_t sector)
{
    rp_function(c, 0xC480, "pops.cd_seek_cycles");
    const uint32_t delta = CD32(current_sector) - sector;
    const uint32_t distance = (int32_t)delta < 0 ? 0u - delta : delta;
    SET32(current_sector, sector);
    const uint32_t flags = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags));
    uint32_t cost;
    if (distance < 7000) {
        cost = (distance * 500 + 10000) / 3;
        if (!(flags & 2)) cost *= 24;
    } else {
        const float root = sqrtf((float)(distance - 6000));
        cost = 1000000 + (uint32_t)root * 6000;
        if (!(flags & 2)) cost <<= 3;
    }
    if (flags & 1) {
        if (!distance) cost = 0;
    } else {
        const uint32_t minimum = flags & 0x20 ? 0x4A6B3 : 0x129ACC;
        if ((int32_t)cost < (int32_t)minimum) cost = minimum;
    }
    if ((CD8(retained_config) & 0x10) && (int32_t)cost > 0x121680) cost = 0x121680;
    if (CD32(drive_event.prev)) {
        const uint32_t remaining = CD32(drive_event.deadline_cycles) - rp_core_guest_cycles(c);
        if ((int32_t)cost < (int32_t)remaining) cost = remaining;
    }
    const uint32_t random = CD32(random_state) * UINT32_C(0x41C64E6D) + 0x3039;
    SET32(random_state, random & 0x7FFFFFFF);
    cost += (128 - (random & 127)) * 32;
    if (sector == rp_u32(c, RP_DEVICE_ADDRESS(c, cd_slow_seek_sector))) cost *= 24;
    SET8(location_pending, 0);
    return CD8(saved_flag) ? cost : 1;
}

static uint32_t find_cached_sector(rp_context *c, uint32_t head, uint32_t sector)
{
    uint32_t node = head;
    do {
        const uint32_t first = rp_u32(c, RP_FIELD_ADDRESS(node, rp_cd_cache_node_layout, first_sector));
        if (sector - first < 16) return node;
        node = rp_u32(c, RP_FIELD_ADDRESS(node, rp_cd_cache_node_layout, next));
    } while (node != head);
    return 0;
}

/* +0xD41C normal-data path: reorder cached blocks or wake the CD worker.
 * Queueing a request is deliberately not treated as completed sector I/O. */
static void request_prefetch(rp_context *c, uint32_t sector)
{
    rp_function(c, 0xD41C, "pops.cd_prefetch_request_partial");
    const uint32_t limit = rp_u32(c, RP_DEVICE_ADDRESS(c, disc_sector_limit));
    if (sector >= limit || rp_u32(c, RP_DEVICE_ADDRESS(c, cd_read_request)) != UINT32_MAX) return;
    if (rp_cd_u8(c, RP_DEVICE_ADDRESS(c, cd_audio_read_pending)))
        rp_block(c, "cd_audio_prefetch_path_not_reconstructed", 0xD588);
    const uint32_t threshold = (uint32_t)rp_pops_msf_to_sector(c, 0x09E8082F) - 150;
    if (sector >= threshold && rp_u32(c, 0x09E80C04)) return;
    sector &= ~UINT32_C(15);
    const uint32_t head = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_cache_head));
    const uint32_t found = find_cached_sector(c, head, sector);
    if (found) {
        const uint32_t next = rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, next));
        const uint32_t prev = rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, prev));
        rp_w32(c, RP_FIELD_ADDRESS(next, rp_cd_cache_node_layout, prev), prev);
        rp_w32(c, RP_FIELD_ADDRESS(prev, rp_cd_cache_node_layout, next), next);
        const uint32_t tail = rp_u32(c, RP_FIELD_ADDRESS(head, rp_cd_cache_node_layout, prev));
        const uint32_t after_tail = rp_u32(c, RP_FIELD_ADDRESS(tail, rp_cd_cache_node_layout, next));
        rp_w32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, prev), tail);
        rp_w32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, next), after_tail);
        rp_w32(c, RP_FIELD_ADDRESS(after_tail, rp_cd_cache_node_layout, prev), found);
        rp_w32(c, RP_FIELD_ADDRESS(tail, rp_cd_cache_node_layout, next), found);
        rp_w32(c, RP_DEVICE_ADDRESS(c, cd_cache_head), found);
        const uint32_t next_sector = sector + 16;
        if (next_sector < limit && !find_cached_sector(c, found, next_sector)) sector = next_sector;
        else {
            sector += 32;
            if (sector >= limit || find_cached_sector(c, found, sector)) return;
        }
    }
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_read_request), sector);
    c->cd_event_bits |= 1;
    ++c->services;
    rp_event(c, "host_adapter", "cd_worker_request_queued", c->cd_thread_entry, sector);
}

/* Cooperative execution of the reached +0xDA3C data-worker iteration. The
 * original thread/512-byte input-cache scheduling remains a host adapter. */
static void service_block_request(rp_context *c)
{
    const uint32_t sector = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_read_request));
    if (sector == UINT32_MAX) return;
    if (rp_cd_u8(c, RP_DEVICE_ADDRESS(c, cd_audio_read_pending)))
        rp_block(c, "cd_audio_worker_not_reconstructed", 0xDCB4);
    rp_function(c, 0xDA3C, "pops.cd_data_worker_iteration_partial");
    if (!(c->cd_event_bits & 1)) rp_block(c, "cd_worker_request_not_signalled", 0xDA84);
    c->cd_event_bits &= ~UINT32_C(1);
    const uint32_t head = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_cache_head));
    const uint32_t node = rp_u32(c, RP_FIELD_ADDRESS(head, rp_cd_cache_node_layout, prev));
    const uint32_t buffer = rp_u32(c, RP_FIELD_ADDRESS(node, rp_cd_cache_node_layout, buffer));
    rp_w32(c, RP_FIELD_ADDRESS(node, rp_cd_cache_node_layout, first_sector), 0x80000000);
    const bool loaded = rp_cd_plain_block_read(c, rp_cd_block_index(sector), buffer);
    if (loaded) {
        rp_w32(c, RP_FIELD_ADDRESS(node, rp_cd_cache_node_layout, first_sector), sector);
        rp_w32(c, RP_DEVICE_ADDRESS(c, cd_cache_head), node);
        rp_event(c, "milestone", "cd_block_cache_published", sector, buffer);
    }
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_read_request), UINT32_MAX);
    c->cd_event_bits |= 2;
    if (!loaded) rp_block(c, "cd_block_read_or_decode_failed", sector);
}

/* +0xD5CC normal data: satisfy a cache miss, promote its node, fix optional
 * sector headers and request the next block. No data-ready flag is set here. */
uint32_t rp_pops_cd_get_sector(rp_context *c, uint32_t sector, bool wait)
{
    rp_function(c, 0xD5CC, "pops.cd_get_cached_sector_partial");
    uint32_t head = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_cache_head));
    uint32_t found = find_cached_sector(c, head, sector);
    if (!found) {
        if (!wait) return 0;
        if (rp_cd_u8(c, RP_DEVICE_ADDRESS(c, cd_audio_read_pending)))
            rp_block(c, "cd_audio_cache_miss_not_reconstructed", 0xD7AC);
        service_block_request(c);
        request_prefetch(c, sector & ~UINT32_C(15));
        if (rp_u32(c, RP_DEVICE_ADDRESS(c, cd_read_request)) == (sector & ~UINT32_C(15)))
            service_block_request(c);
        head = rp_u32(c, RP_DEVICE_ADDRESS(c, cd_cache_head));
        found = find_cached_sector(c, head, sector);
        if (!found) rp_block(c, "cd_sector_retry_not_reconstructed", 0xD5CC);
    }
    if (rp_cd_u8(c, RP_DEVICE_ADDRESS(c, cd_audio_read_pending)))
        rp_block(c, "cd_audio_cache_hit_not_reconstructed", 0xD61C);
    const uint32_t next = rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, next));
    const uint32_t prev = rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, prev));
    rp_w32(c, RP_FIELD_ADDRESS(next, rp_cd_cache_node_layout, prev), prev);
    rp_w32(c, RP_FIELD_ADDRESS(prev, rp_cd_cache_node_layout, next), next);
    const uint32_t tail = rp_u32(c, RP_FIELD_ADDRESS(head, rp_cd_cache_node_layout, prev));
    const uint32_t after_tail = rp_u32(c, RP_FIELD_ADDRESS(tail, rp_cd_cache_node_layout, next));
    rp_w32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, prev), tail);
    rp_w32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, next), after_tail);
    rp_w32(c, RP_FIELD_ADDRESS(after_tail, rp_cd_cache_node_layout, prev), found);
    rp_w32(c, RP_FIELD_ADDRESS(tail, rp_cd_cache_node_layout, next), found);
    rp_w32(c, RP_DEVICE_ADDRESS(c, cd_cache_head), found);
    uint32_t slot = sector - rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, first_sector));
    const uint16_t transform = rp_cd_u16(c,
        RP_FIELD_ADDRESS(rp_cd_block_index(sector), rp_cd_block_index_layout, sector_transform));
    if (transform == 2) {
        slot = 15 - slot;
        slot = ((slot & 5) << 1) | ((slot >> 1) & 5);
        slot = ((slot & 3) << 2) | ((slot >> 2) & 3);
    }
    const uint32_t address = rp_u32(c, RP_FIELD_ADDRESS(found, rp_cd_cache_node_layout, buffer)) +
                             slot * RP_CD_SECTOR_BYTES;
    if (transform) {
        uint8_t *bytes = rp_memory(c, address, sizeof(rp_cd_sector_header_layout));
        bytes[0] = bytes[11] = 0;
        memset(bytes + 1, 0xFF, 10);
        const uint32_t absolute = sector + 150;
        const unsigned msf[] = {absolute / 4500, (absolute / 75) % 60, absolute % 75};
        for (unsigned i = 0; i < 3; ++i)
            bytes[offsetof(rp_cd_sector_header_layout, msf) + i] = (uint8_t)(msf[i] + (msf[i] / 10) * 6);
    }
    request_prefetch(c, sector + 16);
    return address;
}

/* +0xC3A4: prepare a reply but expose it only when its scheduled event runs. */
static uint32_t primary_response(rp_context *c, uint8_t length, uint32_t delay)
{
    rp_function(c, 0xC3A4, "pops.cd_schedule_primary_response");
    SET8(primary.length, length);
    rp_pops_schedule_event(c, RP_CD_ADDRESS(c, primary), delay);
    SET8(primary.payload[0], CD8(drive_status) | CD8(error_flag));
    if (!CD8(error_flag)) {
        SET8(primary.pending_irq, 3);
        return RP_CD_ADDRESS(c, primary.payload);
    }
    SET8(primary.length, 2);
    SET8(primary.payload[1], CD8(error_code));
    SET8(error_flag, 0);
    SET8(primary.pending_irq, 5);
    return 0;
}

static void secondary_response(rp_context *c, uint8_t length, uint32_t delay)
{
    rp_function(c, 0xC418, "pops.cd_schedule_secondary_response");
    const int8_t old = (int8_t)CD8(secondary.pending_irq);
    SET8(secondary.length, length);
    SET8(secondary.pending_irq, 2);
    if (old < 0 && CD32(secondary.event.prev))
        rp_pops_remove_event(c, RP_CD_ADDRESS(c, secondary));
    rp_pops_schedule_event(c, RP_CD_ADDRESS(c, secondary), delay);
}

/* +0xC268: an unacknowledged interrupt leaves the other response pending. */
static void publish_response(rp_context *c, uint32_t event)
{
    rp_function(c, 0xC268, "pops.cd_publish_response");
    uint8_t irq = CD8(irq_flags);
    if (!irq) {
        const int8_t pending = (int8_t)rp_cd_u8(c, RP_CD_RESPONSE_ADDRESS(event, pending_irq));
        rp_w8(c, RP_CD_RESPONSE_ADDRESS(event, pending_irq), 0);
        if (pending < 0) {
            SET8(drive_status, CD8(drive_status) & ~0x40);
        } else {
            uint8_t status = CD8(status_index);
            SET8(response_cursor, 0);
            SET8(response_length, rp_cd_u8(c, RP_CD_RESPONSE_ADDRESS(event, length)));
            /* The original copies three words, including its cleared kind byte. */
            memcpy(rp_memory(c, RP_CD_ADDRESS(c, response_fifo), 12),
                   rp_memory(c, RP_CD_RESPONSE_ADDRESS(event, payload), 12), 12);
            if (event == RP_CD_ADDRESS(c, primary)) {
                status &= 0x7F;
            } else {
                uint8_t drive = CD8(drive_status);
                if (drive & 0x40) {
                    drive &= ~0x40;
                    SET8(drive_status, drive);
                    if (CD8(seek_header_pending)) SET32(header_pointer, 0);
                }
                SET8(command_lock, 0);
                if (CD8(error_flag)) {
                    SET8(response_length, 2);
                    SET8(response_fifo[1], CD8(error_code));
                    drive |= CD8(error_flag);
                    SET8(error_flag, 0);
                }
                SET8(response_fifo[0], drive);
            }
            SET8(status_index, status | 0x20);
            irq = (uint8_t)pending;
            SET8(irq_flags, irq);
        }
    }
    if (irq & CD8(irq_enable)) rp_pops_raise_irq(c, 4);
}

/* +0xC5EC reached Mode-2 data path. XA playback and error reporting still
 * stop at their original branches rather than publishing substitute data. */
static void sector_event(rp_context *c)
{
    rp_function(c, 0xC5EC, "pops.cd_sector_event_partial");
    uint32_t sector = CD32(current_sector);
    const uint8_t mode = CD8(mode);
    SET8(drive_status, (CD8(drive_status) & ~0x40) | 0x20);
    if (!(mode & 0x40) && (CD8(retained_config) & 8) && !(CD8(data_request) & 0x80) &&
            CD8(sector_defer_count) < 3) {
        SET8(sector_defer_count, CD8(sector_defer_count) + 1);
        rp_pops_schedule_event(c, RP_CD_ADDRESS(c, sector_event), mode & 0x80 ? 0x1B900 : 0x37200);
        return;
    }
    SET8(sector_defer_count, 0);
    if (sector >= rp_u32(c, RP_DEVICE_ADDRESS(c, disc_sector_limit)))
        rp_block(c, "cd_sector_end_error_not_reconstructed", 0xC6E0);
    const uint32_t data = rp_pops_cd_get_sector(c, sector, true);
    if (!data || rp_u32(c, data) != 0xFFFFFF00 || rp_u32(c, data + 4) != UINT32_MAX ||
            rp_u32(c, data + 8) != 0x00FFFFFF)
        rp_block(c, "cd_sector_sync_error_not_reconstructed", 0xC6E0);
    SET32(header_pointer, RP_FIELD_ADDRESS(data, rp_cd_sector_header_layout, msf));
    if (rp_cd_u8(c, RP_FIELD_ADDRESS(data, rp_cd_sector_header_layout, mode)) == 2) {
        if ((rp_cd_u8(c, RP_FIELD_ADDRESS(data, rp_cd_sector_header_layout, submode)) & 0x44) == 0x44 && (mode & 0x40))
            rp_block(c, "cd_XA_sector_path_not_reconstructed", 0xC9D0);
        const unsigned buffer = CD8(producer_buffer);
        if (buffer > 1) rp_block(c, "cd_producer_buffer_out_of_range", buffer);
        SET32(sector_buffers[buffer], data);
        SET8(producer_buffer, buffer ^ 1);
        if (!(CD8(status_index) & 0x40)) {
            SET16(data_cursor, mode & 0x20 ? offsetof(rp_cd_sector_header_layout, msf) : sizeof(rp_cd_sector_header_layout));
            SET16(data_limit, mode & 0x30 ? RP_CD_SECTOR_BYTES : sizeof(rp_cd_sector_header_layout) + RP_CD_PAYLOAD_BYTES);
            SET8(status_index, CD8(status_index) | 0x40);
        }
        const int8_t pending = (int8_t)CD8(secondary.pending_irq);
        SET8(secondary.length, 1); SET8(secondary.pending_irq, 1);
        if (pending < 0) {
            if (CD32(secondary.event.prev)) rp_pops_remove_event(c, RP_CD_ADDRESS(c, secondary));
        }
        publish_response(c, RP_CD_ADDRESS(c, secondary));
        rp_event(c, "milestone", "cd_sector_data_published", sector, data);
    }
    if (mode & 0x40) rp_block(c, "cd_XA_sector_timing_not_reconstructed", 0xC868);
    uint32_t delay = CD8(retained_config) & 8 ? 0x37200 : 0x6E400;
    if (mode & 0x80) delay >>= 1;
    if ((rp_u32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags)) & 0x200) && CD8(location_pending)) {
        const uint32_t next = CD32(requested_sector) - 1;
        if (sector != next) {
            sector = next;
            delay = rp_pops_cd_seek_cycles(c, next);
            SET32(playing_sector, 0);
        }
    }
    SET32(current_sector, sector + 1);
    rp_pops_schedule_event(c, RP_CD_ADDRESS(c, sector_event), delay);
    if (!CD8(irq_flags) && (int8_t)CD8(deferred_command) >= 0 && !CD32(primary.event.prev))
        command(c, CD8(deferred_command));
}

/* +0xCE18: CD FIFO to RAM. VFPU copy/clear instructions express memory
 * effects here; cache-maintenance instructions are host-coherent adapters. */
uint32_t rp_pops_cd_dma_transfer(rp_context *c, uint32_t address, uint32_t length, uint32_t control)
{
    rp_function(c, 0xCE18, "pops.CD_DMA_to_RAM");
    if (!(control & 0x10000000)) return length;
    const uint32_t offset = address & 0x1FFFFF;
    if ((offset & 3) || (length & 3) || length > 0x200000 - offset)
        rp_block(c, "CD_DMA_RAM_span_not_reconstructed", address);
    const uint32_t destination = UINT32_C(0x09800000) + offset;
    const uint32_t cursor = CD16(data_cursor), limit = CD16(data_limit);
    const unsigned selected = CD8(selected_buffer);
    if (selected > 1 || cursor > limit)
        rp_block(c, "CD_DMA_FIFO_state_not_reconstructed", 0xCE30);
    const uint32_t requested_end = cursor + length;
    if (requested_end > sizeof(rp_cd_sector_header_layout)) {
        const uint32_t page_end = (destination | 0xFFFF) + 1;
        const uint32_t pages = rp_u32(c, RP_DMA_ADDRESS(c, compiled_ram_pages)) >> (offset >> 16);
        const uint32_t begin = pages & 1 ? destination : page_end;
        uint32_t end = destination + length;
        if (end > page_end && !(pages & 2)) end = page_end;
        if (end > begin) {
            const uint32_t lookup = begin + 0x400000;
            const uint32_t bytes = end - begin;
            uint8_t *target = rp_memory(c, lookup, bytes);
            if (((lookup ^ (lookup + bytes)) & ~UINT32_C(63)) == 0) {
                memset(target, 0, bytes);
            } else {
                if (!c->vfpu_zero_ready)
                    rp_block(c, "CD_DMA_cache_vector_not_initialized", 0xCEBC);
                for (uint32_t i = 0; i < bytes; i += 4)
                    memcpy(target + i, &c->vfpu_reset_rows[3][((lookup + i) >> 2) & 3], 4);
            }
            rp_event(c, "milestone", "CD_DMA_invalidated_RAM_entries", lookup, bytes);
        }
    }
    uint32_t end = requested_end;
    if (end >= limit) {
        end = limit;
        SET8(status_index, CD8(status_index) & ~0x40);
        SET8(data_request, CD8(data_request) & ~0x80);
    }
    const uint32_t copied = end - cursor;
    if (copied & 3) rp_block(c, "CD_DMA_partial_word_tail_not_reconstructed", 0xCF90);
    SET16(data_cursor, end);
    uint8_t *output = rp_memory(c, destination, length);
    if (copied) memcpy(output, rp_memory(c, CD32(sector_buffers[selected]) + cursor, copied), copied);
    if (copied < length) {
        const uint8_t last = copied ? output[copied - 1] : rp_cd_u8(c, destination - 1);
        memset(output + copied, last, length - copied);
    }
    rp_event(c, "milestone", "CD_DMA_bytes_copied", destination, copied);
    return length;
}

void rp_pops_cd_event(rp_context *c, uint32_t event, uint32_t callback)
{
    if (callback == 0xC5EC) { sector_event(c); return; }
    if (callback == 0xC268) { publish_response(c, event); return; }
    if (callback == 0xCE00) {
        rp_function(c, 0xCE00, "pops.cd_drive_ready");
        SET8(lid_phase, 0); SET8(speed_transition, 0);
        SET8(drive_status, CD8(drive_status) | 2);
        return;
    }
    rp_block(c, "cd_event_not_reconstructed", callback);
}

static void command(rp_context *c, unsigned opcode)
{
    rp_function(c, 0xAE5C, "pops.cd_command_partial");
    rp_event(c, "cd_command", "accepted", opcode, CD8(parameter_count));
    const uint8_t required = *(uint8_t *)rp_module_memory(c, 0xD47D4 + opcode, 1);
    SET8(deferred_command, 0xFF);
    if (CD8(parameter_count) < required) { SET8(error_flag, 1); SET8(error_code, 0x20); }
    if ((int8_t)CD8(secondary.pending_irq) == 2) SET8(secondary.pending_irq, 0xFF);
    SET8(status_index, CD8(status_index) | 0x80);
    const uint32_t flags = rp_u32(c, RP_DEVICE_ADDRESS(c, compatibility_flags));
    if (!(flags & 0x10000)) {
        if (opcode == 9 || ((opcode == 6 || opcode == 0x1B) && (flags & 0x200000))) {
            if (!CD8(poll_flag) && !(flags & 0x4000)) {
                SET8(poll_countdown, 20);
                if (flags & 0x800000) SET8(poll_flag, 1);
            }
        } else if (opcode != 0x13 && opcode != 0xE && opcode != 1 && opcode != 2) {
            SET8(poll_flag, 0); SET8(poll_countdown, 0);
        }
    }
    switch (opcode) {
    case 0xE: {
        const uint8_t mode = CD8(parameters[0]);
        const uint8_t changed = CD8(mode) ^ mode;
        (void)primary_response(c, 1, 0x4000);
        SET8(mode, mode);
        if (!CD32(drive_event.prev) && (!(CD8(drive_status) & 2) || (changed & 0x80))) {
            uint32_t delay = 0x3073200;
            if (CD8(drive_status) & 2) {
                SET8(speed_transition, 1);
                delay = (rp_u32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags)) & 0x80) ? 0x204CC00 : 0x2710;
            }
            rp_pops_schedule_event(c, RP_CD_ADDRESS(c, drive_event), delay);
            if (CD32(sector_event.prev)) {
                delay += CD32(sector_event.deadline_cycles) - rp_core_guest_cycles(c);
                reschedule_mode_event(c, RP_CD_ADDRESS(c, sector_event), delay);
            }
        } else if (CD8(speed_transition) == 1 && (changed & 0x80)) {
            SET8(speed_transition, 2);
            const uint32_t remaining = CD32(drive_event.deadline_cycles) - rp_core_guest_cycles(c);
            uint32_t extension = UINT32_C(0x4099800) - remaining * 4;
            if ((int32_t)extension > 0x204CC00) extension = 0x204CC00;
            reschedule_mode_event(c, RP_CD_ADDRESS(c, drive_event), remaining + extension);
        }
        if (changed & 0x41) {
            if (mode & 0x41) rp_w32(c, RP_SHARED_ADDRESS(cd_volume_matrix), CD32(volume_matrix));
            else { rp_pops_audio_pace(c); rp_w32(c, RP_SHARED_ADDRESS(cd_volume_matrix), 0); }
        }
        break;
    }
    case 6: case 0x1B: {
        if (CD8(lid_phase) || (!(CD8(drive_status) & 2) && !CD32(drive_event.prev) && !CD8(location_pending))) {
            SET8(error_flag, 1); SET8(error_code, 0x80);
        }
        if (!primary_response(c, 1, 0x4000)) break;
        if (CD32(sector_event.prev)) {
            if (!CD8(location_pending) && CD32(sector_event.callback) == 0xC5EC) break;
            SET32(playing_sector, 0);
            rp_pops_remove_event(c, RP_CD_ADDRESS(c, sector_event));
            SET8(drive_status, CD8(drive_status) & 0x17);
        }
        uint32_t delay = (CD8(mode) & 0x80) ? 0x3B200 : 0x72400;
        rp_pops_cd_audio_sync(c);
        if (CD8(location_pending)) delay += rp_pops_cd_seek_cycles(c, CD32(requested_sector));
        else if (!(rp_u32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags)) & 0x40)) delay += 0x52B000;
        if (CD8(drive_status) & 0x40) {
            const uint32_t remaining = CD32(seek_deadline) - rp_core_guest_cycles(c);
            if (delay < remaining) {
                delay = remaining;
                if (CD32(secondary.event.prev)) rp_pops_remove_event(c, RP_CD_ADDRESS(c, secondary));
            }
        } else if (!CD8(location_pending)) SET8(drive_status, CD8(drive_status) | 0x40);
        SET32(sector_event.callback, 0xC5EC);
        rp_pops_schedule_event(c, RP_CD_ADDRESS(c, sector_event), delay);
        request_prefetch(c, CD32(current_sector));
        SET8(audio_muted, 0);
        if (CD8(mode) & 0x40) rp_w32(c, RP_SHARED_ADDRESS(cd_volume_matrix), CD32(volume_matrix));
        break;
    }
    case 9: { /* Pause: acknowledgement now, completion after the drive delay. */
        SET8(saved_flag, 1);
        if (CD8(lid_phase) || (CD8(drive_status) & 0x40)) {
            SET8(error_flag, 1); SET8(error_code, 0x80);
        }
        if (!primary_response(c, 1, 0x4000)) break;
        uint32_t delay = 0x5880;
        if (!CD32(sector_event.prev)) {
            if (CD32(secondary.event.prev)) {
                delay = CD32(secondary.event.deadline_cycles) - rp_core_guest_cycles(c);
                if ((int32_t)delay < 0x5880) delay = 0x5880;
            }
        } else {
            delay = CD32(sector_event.deadline_cycles) - rp_core_guest_cycles(c);
            if ((int32_t)delay < 0x5880) delay = 0x5880;
            uint32_t minimum = (int8_t)CD8(drive_status) < 0 ? 0x5EF4C : 0xB9E99;
            if (!(rp_u32(c, RP_DEVICE_ADDRESS(c, cd_timing_flags)) & 4)) minimum *= 6;
            if ((int32_t)delay < (int32_t)minimum) delay = minimum;
        }
        SET8(audio_muted, 1);
        rp_pops_cd_audio_sync(c);
        SET8(current_track, 0); SET32(playing_sector, 0);
        if (CD32(sector_event.prev)) rp_pops_remove_event(c, RP_CD_ADDRESS(c, sector_event));
        SET8(drive_status, CD8(drive_status) & 0x17);
        secondary_response(c, 1, delay);
        rp_event(c, "milestone", "CD_pause_completion_scheduled", 9, delay);
        break;
    }
    case 2: {
        if (CD8(lid_phase)) { SET8(error_flag, 1); SET8(error_code, 0x80); }
        else if (CD8(parameters[1]) >= 0x60 || CD8(parameters[2]) >= 0x75 ||
                 (CD8(parameters[0]) & 15) > 9 || (CD8(parameters[1]) & 15) > 9 ||
                 (CD8(parameters[2]) & 15) > 9) {
            SET8(error_flag, 1); SET8(error_code, 0x10);
        }
        if (!primary_response(c, 1, 0x4000)) break;
        if (!(CD8(drive_status) & 0xE0)) rp_pops_cd_audio_sync(c);
        const int32_t requested = rp_pops_msf_to_sector(c, RP_CD_ADDRESS(c, parameters));
        const uint32_t sector = requested < 0 ? 0 : (uint32_t)requested;
        SET32(requested_sector, sector);
        SET8(location_pending, 1);
        request_prefetch(c, sector);
        if (CD8(drive_status) & 0x40) {
            const uint32_t delay = rp_pops_cd_seek_cycles(c, sector);
            SET32(seek_deadline, rp_core_guest_cycles(c) + delay + 0x4000);
        }
        break;
    }
    case 0x15: case 0x16: {
        (void)primary_response(c, 1, 0x4000);
        SET8(seek_header_pending, opcode - 0x16);
        uint32_t delay;
        if (!(CD8(drive_status) & 0x40) || !CD32(sector_event.prev))
            delay = rp_pops_cd_seek_cycles(c, CD32(requested_sector));
        else delay = 0xB5E99 + CD32(sector_event.deadline_cycles) - rp_core_guest_cycles(c);
        SET32(seek_deadline, rp_core_guest_cycles(c) + delay);
        if (delay < 0x4000) delay = 0x4000;
        secondary_response(c, 1, delay + 0x4000);
        SET32(playing_sector, 0);
        if (CD32(sector_event.prev)) rp_pops_remove_event(c, RP_CD_ADDRESS(c, sector_event));
        SET8(drive_status, (CD8(drive_status) & 0x17) | 0x42);
        break;
    }
    case 1:
        if (!(CD8(drive_status) & 0x10)) {
            if (CD8(poll_countdown)) {
                SET8(poll_countdown, CD8(poll_countdown) - 1);
                if (!CD8(poll_countdown)) { SET8(poll_flag, 1); SET8(poll_countdown, 20); }
            }
        } else if (CD8(lid_phase) == 2) {
            rp_pops_cd_controller_reset(c);
            SET8(drive_status, 0);
            rp_pops_schedule_event(c, RP_CD_ADDRESS(c, drive_event), 0x3073200);
            SET8(lid_phase, 3);
        }
        (void)primary_response(c, 1, 0x4000);
        break;
    case 0xA: case 0x1E: {
        const uint32_t random = CD32(random_state) * UINT32_C(0x41C64E6D) + 0x3039;
        SET32(random_state, random & 0x7FFFFFFF);
        uint32_t delay = (random & 0xFFFF) * 33 + 0x800000;
        if (CD32(sector_event.prev)) delay += CD32(sector_event.deadline_cycles) - rp_core_guest_cycles(c);
        if (CD32(drive_event.prev)) {
            const uint32_t remaining = CD32(drive_event.deadline_cycles) - rp_core_guest_cycles(c);
            if (delay < remaining) delay = remaining;
        } else if (!(CD8(drive_status) & 2)) delay = 0x204CC00;
        rp_pops_cd_controller_reset(c);
        (void)primary_response(c, 1, 0x4000);
        SET8(command_lock, opcode == 0xA ? 1 : 2);
        secondary_response(c, 1, delay);
        break;
    }
    case 0x1C:
        SET32(random_state, (CD32(random_state) * UINT32_C(0x41C64E6D) + 0x3039) & 0x7FFFFFFF);
        rp_pops_cd_controller_reset(c);
        (void)primary_response(c, 1, 0x4000);
        break;
    case 0xB: case 0xC:
        (void)primary_response(c, 1, 0x4000);
        SET8(audio_muted, opcode == 0xB);
        rp_w32(c, RP_SHARED_ADDRESS(cd_volume_matrix), opcode == 0xB ? 0 : CD32(volume_matrix));
        break;
    case 0xD:
        (void)primary_response(c, 1, 0x4000);
        SET16(filter, CD8(parameters[0]) | (uint16_t)CD8(parameters[1]) << 8);
        break;
    case 0xF: {
        const uint32_t out = primary_response(c, 5, 0x4000);
        if (!out) rp_block(c, "cd_getparam_error_destination", 0xB5F0);
        rp_w8(c, out + 1, CD8(mode)); rp_w8(c, out + 2, CD8(error_code));
        rp_w8(c, out + 3, (uint8_t)CD16(filter)); rp_w8(c, out + 4, (uint8_t)(CD16(filter) >> 8));
        break;
    }
    case 0x19: {
        const unsigned sub = CD8(parameters[0]);
        if (sub >= 0x26) break;
        const uint32_t target = rp_module_u32(c, 0xD488C + sub * 4);
        unsigned length; uint32_t source;
        switch (target) {
        case 0xBD54: length = 1; source = 0; break;
        case 0xBD80: length = 2; source = 0xD480C; break;
        case 0xBD90: length = 4; source = 0xD4808; break;
        case 0xBDA0: length = 9; source = 0xD47F4; break;
        case 0xBDB0: length = 8; source = 0xD4800; break;
        default: rp_block(c, "cd_test_subcommand_not_reconstructed", sub);
        }
        const uint32_t out = primary_response(c, (uint8_t)length, 0x4000);
        if (source) {
            if (!out) rp_block(c, "cd_test_error_destination", 0xBD70);
            memcpy(rp_memory(c, out, length), rp_module_memory(c, source, length), length);
        }
        break;
    }
    case 0: case 0x17: case 0x18: case 0x1D:
        SET8(error_flag, 1); SET8(error_code, 0x40);
        (void)primary_response(c, 1, 0x4000);
        break;
    default: rp_block(c, "cd_command_not_reconstructed", opcode);
    }
    SET8(parameter_count, 0);
}

uint32_t rp_pops_cd_read(rp_context *c, uint32_t address, uint32_t width)
{
    rp_function(c, 0xD088, "pops.cd_read_register");
    uint32_t value = 0;
    switch (address & 15) {
    case 0:
        value = CD8(status_index);
        if (!(CD8(data_request) & 0x80)) value &= ~0x40;
        if (CD32(playing_sector)) value |= 4;
        if (CD8(parameter_count) < 4) {
            if (!CD8(parameter_count)) value |= 8;
            value |= 0x10;
        }
        break;
    case 1:
        if (CD8(status_index) & 0x20) {
            const uint8_t index = CD8(response_cursor);
            SET8(response_cursor, index + 1);
            value = rp_cd_u8(c, RP_CD_ADDRESS(c, response_fifo) + index);
            if (CD8(response_cursor) == CD8(response_length)) SET8(status_index, CD8(status_index) - 0x20);
        }
        break;
    case 2: {
        uint32_t next = CD16(data_cursor) + 1;
        const uint32_t limit = CD16(data_limit);
        if (CD8(selected_buffer) > 1) rp_block(c, "cd_data_buffer_index_not_reconstructed", 0xD0E8);
        const uint32_t buffer = rp_u32(c, RP_CD_ADDRESS(c, sector_buffers) + CD8(selected_buffer) * sizeof(uint32_t));
        if (next >= limit) { next = limit; SET8(status_index, CD8(status_index) & ~0x40); SET8(data_request, CD8(data_request) & 0x7F); }
        SET16(data_cursor, next);
        value = rp_cd_u8(c, buffer + next - 1);
        break;
    }
    case 3:
        if (!(CD8(status_index) & 3)) value = CD8(irq_enable);
        else if ((CD8(status_index) & 3) == 1) value = CD8(irq_flags) | 0xE0;
        break;
    default: break;
    }
    return width ? value : (value ^ UINT32_C(0x80)) - UINT32_C(0x80);
}

void rp_pops_cd_write(rp_context *c, uint32_t address, uint32_t raw)
{
    rp_function(c, 0xD1B0, "pops.cd_write_register_partial");
    const unsigned reg = address & 15;
    const uint8_t value = (uint8_t)raw;
    if (reg >= 4) return;
    const unsigned index = reg + (CD8(status_index) & 3) * 4;
    rp_event(c, "cd_register_write", "banked_register", index, value);
    if (!reg) { SET8(status_index, (CD8(status_index) & ~3) | (value & 3)); return; }
    switch (index) {
    case 1: {
        if (CD8(command_lock)) return;
        const unsigned op = value < 31 ? value : 0;
        if (CD8(irq_flags) || (int8_t)CD8(primary.pending_irq) ||
            (CD32(sector_event.prev) && (int32_t)(CD32(sector_event.deadline_cycles) - rp_core_guest_cycles(c)) <= 0xC672)) {
            SET8(deferred_command, op); return;
        }
        command(c, op); return;
    }
    case 2:
        if (!CD8(command_lock) && CD8(parameter_count) < 4) {
            const unsigned count = CD8(parameter_count);
            rp_w8(c, RP_CD_ADDRESS(c, parameters) + count, value);
            SET8(parameter_count, count + 1);
        }
        return;
    case 3:
        if (value & 0x80) {
            SET8(selected_buffer, CD8(producer_buffer) ^ 1);
            if (!(CD8(data_request) & 0x80)) {
                const uint8_t mode = CD8(mode);
                SET16(data_cursor, mode & 0x20 ? 12 : 24);
                SET16(data_limit, mode & 0x30 ? 0x930 : 0x818);
            }
        }
        SET8(data_request, value); return;
    case 6:
        SET8(irq_enable, value);
        if (value & CD8(irq_flags)) rp_pops_raise_irq(c, 4);
        return;
    case 7: {
        if (value & 0x40) SET8(parameter_count, 0);
        if (value & 0x20) { rp_pops_cd_audio_sync(c); SET32(playing_sector, 0); }
        SET8(irq_flags, CD8(irq_flags) & ~value);
        if (CD8(irq_flags)) return;
        const uint32_t replies[] = {RP_CD_ADDRESS(c, secondary), RP_CD_ADDRESS(c, primary)};
        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t event = replies[i];
            const bool linked = rp_u32(c, RP_CD_RESPONSE_ADDRESS(event, event.prev)) != 0;
            if (linked) { if (i) return; else continue; }
            if ((int8_t)rp_cd_u8(c, RP_CD_RESPONSE_ADDRESS(event, pending_irq))) {
                rp_pops_cd_event(c, event, rp_u32(c, RP_CD_RESPONSE_ADDRESS(event, event.callback)));
                return;
            }
        }
        if ((int8_t)CD8(deferred_command) >= 0) command(c, CD8(deferred_command));
        return;
    }
    case 10: case 11:
        rp_w8(c, RP_CD_ADDRESS(c, volume_matrix) + index - 10, value); return;
    case 13: case 14:
        rp_w8(c, RP_CD_ADDRESS(c, volume_matrix) + index - 11, value); return;
    case 15:
        if ((value & 0x20) && !CD8(audio_muted)) {
            rp_pops_audio_pace(c);
            rp_w32(c, RP_SHARED_ADDRESS(cd_volume_matrix), CD32(volume_matrix));
        }
        return;
    default: return;
    }
}
