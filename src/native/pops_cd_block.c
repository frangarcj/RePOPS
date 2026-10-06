#include "pops_cdrom.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Format adapter for the already admitted plain/homebrew PSISOIMG path.
 * Its compressed blocks are raw DEFLATE, NOT Sony's +0xE02C range decoder.
 * Worker/cache state and completion publication belong to pops_cdrom.c. */
bool rp_cd_plain_block_read(rp_context *c, uint32_t index, uint32_t destination)
{
    if (!c->disc_header.valid || c->data_psp_word != UINT32_C(0x464C457F))
        rp_block(c, "cd_original_block_codec_not_reconstructed", 0xE02C);
    const uint32_t offset = rp_u32(c, RP_FIELD_ADDRESS(index, rp_cd_block_index_layout, file_offset));
    const uint32_t length = rp_cd_u16(c, RP_FIELD_ADDRESS(index, rp_cd_block_index_layout, encoded_bytes));
    uint8_t *output = rp_memory(c, destination, RP_CD_BLOCK_BYTES);
    if (!length) {
        memset(output, 0, RP_CD_BLOCK_BYTES);
        return true;
    }
    if (!c->disc || (uint64_t)offset + length > c->disc_bytes) return false;
    uint8_t *input = malloc(length);
    if (!input) rp_block(c, "cd_block_allocation_failed", index);
    ++c->services;
    rp_event(c, "format_adapter", "plain_cd_block_read", offset, length);
    const bool read_ok = fseek(c->disc, (long)offset, SEEK_SET) == 0 &&
                         fread(input, 1, length, c->disc) == length;
    bool decoded = false;
    if (read_ok && length == RP_CD_BLOCK_BYTES) {
        memcpy(output, input, RP_CD_BLOCK_BYTES);
        decoded = true;
    } else if (read_ok) {
        z_stream stream = {0};
        stream.next_in = input; stream.avail_in = length;
        stream.next_out = output; stream.avail_out = RP_CD_BLOCK_BYTES;
        if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
            const int status = inflate(&stream, Z_FINISH);
            decoded = status == Z_STREAM_END && stream.total_out == RP_CD_BLOCK_BYTES;
            rp_event(c, "format_adapter", "raw_deflate_block_result", (uint32_t)status,
                     (uint32_t)stream.total_out);
            inflateEnd(&stream);
        }
    }
    free(input);
    return decoded;
}
