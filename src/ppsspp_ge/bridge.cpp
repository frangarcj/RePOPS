#include "bridge.h"
#include <cstdio>
#include <algorithm>
#include "Common/Log/LogManager.h"
#include "Common/Thread/ThreadManager.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceGe.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/Software/SoftGpu.h"

static SoftGPU *device;
static char last_error[160];
static int fail(const char *operation, uint32_t value) {
    std::snprintf(last_error, sizeof(last_error), "%s: 0x%08x", operation, value);
    return -1;
}

extern "C" int rp_ppsspp_ge_open(void) {
    if (device) return fail("GE already open", 0);
    g_Config.bEnableLogging = true;
    g_Config.bSoftwareRendering = true;
    g_Config.bSoftwareRenderingJit = false;
    g_Config.bVertexDecoderJit = false;
    g_logManager.Init(&g_Config.bEnableLogging, true);
    g_threadManager.Init(1, 1);
    Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;
    if (!Memory::Init(Memory::MemMapSetupFlags::Default)) return fail("Memory init", 0);
    // A MIPSState supplies the GE's tick counter, but no CPU run function is called.
    PSP_CoreParameter().cpuCore = CPUCore::INTERPRETER;
    mipsr4k.Reset();
    CoreTiming::Init(&mipsr4k);
    __InterruptsInit();
    __GeInit();
    device = new SoftGPU(nullptr, nullptr);
    gpu = device;
    device->EnableInterrupts(false);
    coreState = CORE_RUNNING_GE;
    last_error[0] = 0;
    return 0;
}

extern "C" void rp_ppsspp_ge_close(void) {
    if (!device) return;
    device->FlushPendingDrawing();
    delete device;
    device = nullptr;
    gpu = nullptr;
    __GeShutdown();
    __InterruptsShutdown();
    CoreTiming::Shutdown();
    Memory::Shutdown();
    g_threadManager.Teardown();
    g_logManager.Shutdown();
}

extern "C" void *rp_ppsspp_ge_memory(uint32_t address, size_t bytes) {
    if (!device || bytes > UINT32_MAX || !Memory::IsValidRange(address, (uint32_t)bytes)) {
        fail("Unmapped GE memory", address);
        return nullptr;
    }
    return Memory::GetPointerWriteUnchecked(address);
}

extern "C" int rp_ppsspp_ge_enqueue(uint32_t start, uint32_t stall) {
    if (!device) return fail("GE not open", start);
    bool run = false;
    const uint32_t id = device->EnqueueList(start, stall, -1, PSPPointer<PspGeListArgs>{}, false, &run);
    if (id & 0x80000000u) return fail("Enqueue", id);
    if (run) device->ProcessDLQueue();
    device->FlushPendingDrawing();
    return (int)id;
}

extern "C" int rp_ppsspp_ge_stall(int id, uint32_t stall) {
    if (!device) return fail("GE not open", stall);
    bool run = false;
    const uint32_t result = device->UpdateStall(id, stall, &run);
    if (result & 0x80000000u) return fail("Stall", result);
    if (run) device->ProcessDLQueue();
    device->FlushPendingDrawing();
    return 0;
}

extern "C" int rp_ppsspp_ge_sync(int id) {
    if (!device) return fail("GE not open", (uint32_t)id);
    device->ProcessDLQueue();
    device->FlushPendingDrawing();
    // Poll only: never enter PSP scheduler waits or invent a completion.
    const int result = device->ListSync(id, 1);
    if (result != 0) return fail("List incomplete", (uint32_t)result);
    // Drawing really finished above. Retire the donor's scheduled completion
    // metadata at its own GE-derived tick, without changing RePops guest time
    // or executing any MIPS instructions. Otherwise 64 used ids never recycle.
    const int64_t finish = device->GetListTicks(id);
    while (finish >= 0 && CoreTiming::GetTicks(&mipsr4k) <= (uint64_t)finish) {
        const uint64_t left = (uint64_t)finish + 1 - CoreTiming::GetTicks(&mipsr4k);
        mipsr4k.downcount -= (int)std::min<uint64_t>(left, 1000000);
        CoreTiming::Advance(&mipsr4k);
    }
    return 0;
}

extern "C" const char *rp_ppsspp_ge_error(void) { return last_error; }
