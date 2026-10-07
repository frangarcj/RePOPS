#include "bridge.h"
#include "GPU/ge_constants.h"
#include <cstdio>
#include <cstring>
#include <vector>

static uint32_t cmd(GECommand op, uint32_t value = 0) { return ((uint32_t)op << 24) | (value & 0xFFFFFF); }
static bool check(bool value, const char *message) {
    if (!value) std::fprintf(stderr, "FAIL: %s (%s)\n", message, rp_ppsspp_ge_error());
    return value;
}
int main() {
    if (!check(rp_ppsspp_ge_open() == 0, "GE startup")) return 1;
    auto *low = static_cast<uint32_t *>(rp_ppsspp_ge_memory(0x04008000, 4));
    auto *high = static_cast<uint32_t *>(rp_ppsspp_ge_memory(0x04208000, 4));
    auto *alias = static_cast<uint32_t *>(rp_ppsspp_ge_memory(0x44208000, 4));
    if (!check(low && high && alias, "4 MiB EDRAM mapping")) return 1;
    *low = 0x12345678; *high = 0x87654321;
    if (!check(*low == 0x12345678 && *alias == 0x87654321, "upper EDRAM is distinct with coherent alias")) return 1;
    *low = *high = 0;
    const uint32_t list_addr = 0x08010000, vertex_addr = 0x08020000;
    auto *list = static_cast<uint32_t *>(rp_ppsspp_ge_memory(list_addr, 4096));
    auto *fb = static_cast<uint16_t *>(rp_ppsspp_ge_memory(0x04000000, 512 * 256 * 2));
    struct Vertex { uint32_t color; float x, y, z; };
    const Vertex vertices[] = {{0xFF0000FF, 8, 8, 0}, {0xFF0000FF, 24, 24, 0}};
    void *v = rp_ppsspp_ge_memory(vertex_addr, sizeof(vertices));
    if (!check(list && fb && v, "GE memory")) return 1;
    std::memcpy(v, vertices, sizeof(vertices));
    const std::vector<uint32_t> commands = {
        cmd(GE_CMD_FRAMEBUFPTR, 0), cmd(GE_CMD_FRAMEBUFWIDTH, 0x040200),
        cmd(GE_CMD_FRAMEBUFPIXFORMAT, GE_FORMAT_5551),
        cmd(GE_CMD_REGION1), cmd(GE_CMD_REGION2, 511 | (255 << 10)),
        cmd(GE_CMD_SCISSOR1), cmd(GE_CMD_SCISSOR2, 511 | (255 << 10)),
        cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_THROUGH),
        cmd(GE_CMD_BASE, 0x080000), cmd(GE_CMD_VADDR, vertex_addr & 0xFFFFFF),
        cmd(GE_CMD_CLEARMODE, 0x301),
        cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2),
        cmd(GE_CMD_CLEARMODE, 0), cmd(GE_CMD_FINISH), cmd(GE_CMD_END),
    };
    std::memcpy(list, commands.data(), commands.size() * 4);
    const int id = rp_ppsspp_ge_enqueue(list_addr, list_addr);
    if (!check(id >= 0 && fb[10 * 512 + 10] == 0, "queue stalls before drawing")) return 1;
    if (!check(rp_ppsspp_ge_stall(id, 0) == 0 && rp_ppsspp_ge_sync(id) == 0, "execute rectangle list")) return 1;
    if (!check(fb[10 * 512 + 10] == 0x801F && fb[7 * 512 + 7] == 0, "rasterized red pixel and clip")) {
        std::fprintf(stderr, "pixel=%04x\n", fb[10 * 512 + 10]);
        return 1;
    }
    const uint32_t output = 0x08030000;
    auto *pixels = static_cast<uint16_t *>(rp_ppsspp_ge_memory(output, 512));
    std::memset(pixels, 0xA5, 512);
    const std::vector<uint32_t> transfer = {
        cmd(GE_CMD_TRANSFERSRC, 0), cmd(GE_CMD_TRANSFERSRCW, 0x040200),
        cmd(GE_CMD_TRANSFERDST, output & 0xFFFFFF), cmd(GE_CMD_TRANSFERDSTW, 0x080010),
        cmd(GE_CMD_TRANSFERSRCPOS, 8 | (8 << 10)), cmd(GE_CMD_TRANSFERDSTPOS),
        cmd(GE_CMD_TRANSFERSIZE, 15 | (15 << 10)), cmd(GE_CMD_TRANSFERSTART),
        cmd(GE_CMD_FINISH), cmd(GE_CMD_END),
    };
    std::memcpy(list + 128, transfer.data(), transfer.size() * 4);
    const int transfer_id = rp_ppsspp_ge_enqueue(list_addr + 512, 0);
    if (!check(transfer_id >= 0 && rp_ppsspp_ge_sync(transfer_id) == 0, "GE transfer to RAM")) return 1;
    for (unsigned i = 0; i < 256; ++i)
        if (!check(pixels[i] == 0x801F, "GE readback matches rendered pixels")) return 1;
    for (unsigned i = 0; i < 80; ++i) {
        const int repeated = rp_ppsspp_ge_enqueue(list_addr + 512, 0);
        if (!check(repeated >= 0 && rp_ppsspp_ge_sync(repeated) == 0,
                   "completed GE ids recycle beyond queue capacity")) return 1;
    }
    rp_ppsspp_ge_close();
    std::puts("PPSSPP GE: stalled queue, software-rasterized rectangle and 512-byte nonzero readback passed; no PSP CPU instructions executed.");
    return 0;
}
