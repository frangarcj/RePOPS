#ifndef REPOPS_CDROM_H
#define REPOPS_CDROM_H
#include "pops_state.h"

/* Recovered wire views; all addresses remain guest u32 values. */
typedef struct {
    uint32_t next, prev, deadline_cycles, callback;
} rp_guest_event_layout;

typedef struct {
    rp_guest_event_layout event;
    uint8_t payload[10];
    int8_t pending_irq;
    uint8_t length;
} rp_cd_response_layout;

typedef struct {
    rp_cd_response_layout primary, secondary;
    rp_guest_event_layout unknown_event, sector_event, drive_event;
    uint8_t response_fifo[12], response_length, parameter_count, parameters[4];
    uint16_t data_cursor, data_limit;
    int8_t deferred_command;
    uint8_t current_track, audio_muted, unknown_81, producer_buffer, selected_buffer;
    uint8_t error_flag, error_code, command_lock, unknown_87, location_pending;
    uint8_t response_cursor, poll_flag, lid_phase, poll_countdown, retained_config;
    uint8_t sector_defer_count, saved_flag, speed_transition, seek_header_pending;
    uint8_t unknown_92[2];
    uint32_t sector_buffers[2];
    uint8_t mode, drive_status;
    uint16_t filter;
    uint32_t random_state, seek_deadline, current_sector, requested_sector;
    uint32_t playing_sector, header_pointer;
    uint8_t volume_matrix[4], status_index, irq_enable, irq_flags, data_request;
} rp_cdrom_layout;

typedef struct { uint32_t read, write; } rp_io_handler_layout;
typedef struct { uint32_t next, prev, first_sector, buffer; } rp_cd_cache_node_layout;
typedef struct {
    uint32_t file_offset;
    uint16_t encoded_bytes, sector_transform;
    uint8_t integrity_data[16];
    int32_t aligned_read_bytes;
    uint32_t reserved;
} rp_cd_block_index_layout;
typedef struct {
    uint8_t sync[12], msf[3], mode;
    uint8_t file, channel, submode, coding, repeated_subheader[4];
} rp_cd_sector_header_layout;

enum {
    RP_CD_SECTOR_BYTES = 2352, RP_CD_PAYLOAD_BYTES = 2048, RP_CD_BLOCK_SECTORS = 16,
    RP_CD_BLOCK_BYTES = RP_CD_SECTOR_BYTES * RP_CD_BLOCK_SECTORS,
    RP_CD_INDEX_BASE = 0x09E84000
};
static inline uint32_t rp_cd_block_index(uint32_t sector)
{ return RP_CD_INDEX_BASE + (sector / RP_CD_BLOCK_SECTORS) * sizeof(rp_cd_block_index_layout); }
typedef struct {
    uint8_t unknown_000[0x130];
    uint32_t cpu_status, cpu_cause;
    uint8_t unknown_138[0x6AC - 0x138];
    uint32_t compatibility_flags;
    uint8_t unknown_6b0[0x6C0 - 0x6B0];
    uint32_t cd_timing_flags;
    uint8_t unknown_6c4[0x6EC - 0x6C4];
    uint32_t cd_slow_seek_sector;
    uint8_t unknown_6f0[0x730 - 0x6F0];
    uint32_t disc_sector_limit;
    uint8_t unknown_734[0x1000 - 0x734];
    rp_io_handler_layout io_handlers[512];
    union {
        uint8_t io_register_shadow[0x1000];
        struct {
            uint8_t unknown_2000[0x70];
            uint32_t irq_status, irq_mask;
        };
    };
    uint8_t unknown_3000[0x3608 - 0x3000];
    uint32_t audio_sample_origin, audio_cycle_origin;
    uint8_t unknown_3610[0x3668 - 0x3610];
    uint32_t display_mode;
    uint8_t unknown_366c[0x3800 - 0x366C];
    rp_cdrom_layout cd;
    uint8_t unknown_38c0[0x3D00 - 0x38C0];
    rp_cd_cache_node_layout cd_cache[17];
    uint32_t cd_cache_head, cd_read_request;
    uint8_t unknown_3e18[0x3E3C - 0x3E18];
    uint32_t cd_read_event, cd_file_handle;
    uint8_t cd_disc_count, cd_selected_disc;
    uint8_t cd_audio_read_pending;
} rp_core_device_layout;

#define RP_DEVICE_ADDRESS(c, member) RP_FIELD_ADDRESS((c)->gp, rp_core_device_layout, member)
#define RP_CD_ADDRESS(c, member) RP_DEVICE_ADDRESS(c, cd.member)
#define RP_CD_RESPONSE_ADDRESS(base, member) RP_FIELD_ADDRESS(base, rp_cd_response_layout, member)

static inline uint8_t rp_cd_u8(rp_context *c, uint32_t address)
{ return *(uint8_t *)rp_memory(c, address, 1); }
static inline uint16_t rp_cd_u16(rp_context *c, uint32_t address)
{
    const uint8_t *p = rp_memory(c, address, 2);
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static inline void rp_cd_w16(rp_context *c, uint32_t address, uint16_t value)
{
    uint8_t *p = rp_memory(c, address, 2);
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static inline uint32_t rp_device_handler(rp_context *c, uint32_t address, bool write)
{
    uint32_t index = (address + UINT32_C(0xE07FF000)) >> 3;
    if (index > 511) index = 511;
    const uint32_t pair = RP_DEVICE_ADDRESS(c, io_handlers) + index * sizeof(rp_io_handler_layout);
    return rp_u32(c, pair + (write ? offsetof(rp_io_handler_layout, write) :
                                   offsetof(rp_io_handler_layout, read)));
}

void rp_pops_cd_write(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_cd_read(rp_context *, uint32_t, uint32_t);
void rp_pops_cd_event(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_cd_seek_cycles(rp_context *, uint32_t);
void rp_pops_cd_controller_reset(rp_context *);
void rp_pops_cd_audio_sync(rp_context *);
void rp_pops_audio_pace(rp_context *);
void rp_pops_raise_irq(rp_context *, uint32_t);
void rp_pops_shadow_write(rp_context *, uint32_t, uint32_t, uint32_t);
bool rp_cd_plain_block_read(rp_context *, uint32_t, uint32_t);
uint32_t rp_pops_cd_get_sector(rp_context *, uint32_t, bool);

_Static_assert(sizeof(rp_cd_block_index_layout) == 32, "CD block index stride");
_Static_assert(sizeof(rp_cd_sector_header_layout) == 24, "Mode-2 sector prefix");
_Static_assert(offsetof(rp_cd_block_index_layout, aligned_read_bytes) == 24, "CD prepared read size");
_Static_assert(sizeof(rp_cd_response_layout) == 0x1C, "CD response layout");
_Static_assert(sizeof(rp_cdrom_layout) == 0xC0, "CD controller layout");
_Static_assert(offsetof(rp_cdrom_layout, response_fifo) == 0x68, "CD response FIFO");
_Static_assert(offsetof(rp_cdrom_layout, status_index) == 0xBC, "CD port state");
_Static_assert(offsetof(rp_core_device_layout, io_handlers) == 0x1000, "I/O table");
_Static_assert(offsetof(rp_core_device_layout, irq_status) == 0x2070, "I_STAT shadow");
_Static_assert(offsetof(rp_core_device_layout, cd) == 0x3800, "CD core placement");
_Static_assert(offsetof(rp_core_device_layout, cd_cache_head) == 0x3E10, "CD cache root");
_Static_assert(offsetof(rp_core_device_layout, cd_audio_read_pending) == 0x3E46, "CD worker pending");
#endif
