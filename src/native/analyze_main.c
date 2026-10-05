/* Standalone driver for reconstructed POPS analysis, not a PS1 executor.
 * Uses the same locally prepared data image as repops-native.
 */
#include "runtime.h"
#include "pops_emit.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const int flow_emission = argc == 4 && strcmp(argv[3], "--emit-flow") == 0;
    const int memory_emission = flow_emission || (argc == 4 && strcmp(argv[3], "--emit-memory") == 0);
    const int emission = memory_emission || (argc == 4 && strcmp(argv[3], "--emit-immediates") == 0);
    const int prepare = emission || (argc == 4 && strcmp(argv[3], "--prepare") == 0);
    if (prepare) --argc;
    if (argc != 3) {
        fprintf(stderr, "usage: %s <checked-native-image.bin> <new-output-directory> [--prepare|--emit-immediates|--emit-memory|--emit-flow]\n", argv[0]);
        return 64;
    }
    rp_context *c = calloc(1, sizeof(*c));
    if (!c) return 70;
    c->regions[0] = (rp_region){0, 0x800000, calloc(1, 0x800000)};
    c->regions[2] = (rp_region){0x08000000, 0x2000000, calloc(1, 0x2000000)};
    c->regions[3] = (rp_region){0x04000000, 0x400000, calloc(1, 0x400000)};
    for (unsigned i = 0; i < RP_REGION_COUNT; ++i)
        if (c->regions[i].size && !c->regions[i].bytes) return 70;
    FILE *image = fopen(argv[1], "rb");
    if (!image) { perror("image"); return 66; }
    size_t image_size = fread(c->regions[0].bytes, 1, c->regions[0].size, image);
    fclose(image);
    if (image_size < 0xD5000) { fputs("Incomplete native data image\n", stderr); return 65; }
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/trace.jsonl", argv[2]) >= (int)sizeof(path)) return 64;
    c->trace = fopen(path, "wx");
    if (!c->trace) { perror(path); return 73; }
    c->gp = 0x10000;
    const uint32_t pc = 0xBFC00000, buffer = 0x041B0000;
    rp_w32(c, c->gp + 0x130, 0x400000);
    rp_w32(c, c->gp + 0x6F4, UINT32_MAX);
    if (prepare) rp_w32(c, c->gp + 0x1D0, 0x09B80000);
    /* BIOS setup observed in +0x058C0 before it calls +0x05154. */
    rp_w32(c, c->gp + 0xB40, 0x00028000);
    rp_w32(c, c->gp + 0xB48, UINT32_C(0x00053C20) - pc);
    rp_w32(c, c->gp + 0xB4C, buffer);
    rp_w32(c, c->gp + 0xB50, pc);
    rp_w32(c, c->gp + 0xB54, pc + 0xC00);
    if (memory_emission) {
        /* Probe input from the native reset's +0x1A0A8 table: default handlers,
         * with register-backed overrides at +0x1000..103F and +0x1060..106F.
         * This is an input fixture, not running the full reset in this probe.
         */
        for (unsigned slot = 0; slot < 512; ++slot) {
            const bool registers = slot < 8 || slot == 12 || slot == 13;
            rp_w32(c, c->gp + 0x1000 + slot * 8, registers ? 0x8A54 : 0x88BC);
            rp_w32(c, c->gp + 0x1004 + slot * 8, registers ? 0x8AA4 : 0x89A0);
        }
    }
    int status = 0;
    uint32_t emission_cursor = 0;
    uint32_t emission_start = 0, emitted_records = 0;
    if (setjmp(c->stop) == 0) {
        if (prepare) emission_cursor = rp_pops_prepare_compile(c, pc);
        else rp_pops_analyze_records(c, buffer);
        if (emission) {
            emission_start = emission_cursor;
            rp_emit_init_registers(c, emission_cursor);
            /* Focused emitter probe, not the complete +0x058C0 record walk.
             * Stop before the first category that is not reconstructed yet.
             */
            for (uint32_t record = buffer; record <= rp_u32(c, c->gp + 0xB4C); record += 16) {
                const uint8_t *r = rp_memory(c, record, 16);
                const uint32_t category = r[4] | (uint32_t)r[5] << 8;
                if (category == 0) continue;
                if (category != 9 && category != 0x13 && !(memory_emission && category == 0x10) &&
                        !(flow_emission && (category == 0xE || category == 0xD || category == 0xA || category == 5))) {
                    rp_event(c, "probe_boundary", "next_emitter_category", record, category);
                    break;
                }
                emission_cursor = rp_emit_record(c, category, record, emission_cursor, 0);
                ++emitted_records;
            }
        }
    }
    else {
        fprintf(stderr, "Analysis blocked: %s @ 0x%08X\n", c->stop_kind, c->stop_address);
        status = 78;
    }
    if (!status) {
        const uint32_t end = rp_u32(c, c->gp + 0xB4C);
        const uint32_t bytes = end - buffer + 16;
        snprintf(path, sizeof(path), "%s/records.bin", argv[2]);
        FILE *records = fopen(path, "wx");
        if (!records || fwrite(rp_memory(c, buffer, bytes), 1, bytes, records) != bytes) return 74;
        fclose(records);
        snprintf(path, sizeof(path), "%s/scratch.bin", argv[2]);
        FILE *scratch = fopen(path, "wx");
        if (!scratch || fwrite(c->scratchpad, 1, sizeof(c->scratchpad), scratch) != sizeof(c->scratchpad)) return 74;
        fclose(scratch);
        if (emission) {
            snprintf(path, sizeof(path), "%s/allegrex.bin", argv[2]);
            FILE *code = fopen(path, "wx");
            const size_t size = emission_cursor - emission_start;
            if (!code || fwrite(rp_memory(c, emission_start, size), 1, size, code) != size) return 74;
            fclose(code);
        }
        snprintf(path, sizeof(path), "%s/analysis.json", argv[2]);
        FILE *report = fopen(path, "wx");
        if (!report) return 74;
        fprintf(report, "{\"guest_start\":%u,\"record_buffer\":%u,\"high_water\":%u,"
                "\"record_slots\":%u,\"native_function_entries\":%u,\"guest_executed\":false,"
                "\"emission_cursor_not_executable\":%u,\"emitted_records\":%u,\"stage\":\"%s\"}\n",
                pc, buffer, end, bytes / 16, c->functions, emission_cursor, emitted_records,
                flow_emission ? "native_C_POPS_forward_flow_and_known_ALU_probe" :
                memory_emission ? "native_C_POPS_known_memory_emitter_probe" :
                emission ? "native_C_POPS_immediate_emitter_probe" :
                prepare ? "native_C_POPS_058C0_through_05D5B" : "native_C_reconstruction_of_POPS_05154");
        fclose(report);
        printf("POPS analysis: %u record slots, %u native function entries, high-water 0x%08X; no guest execution\n",
               bytes / 16, c->functions, end);
    }
    fclose(c->trace);
    for (unsigned i = 0; i < RP_REGION_COUNT; ++i) free(c->regions[i].bytes);
    free(c);
    return status;
}
