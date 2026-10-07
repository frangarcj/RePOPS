// Platform-only shim adapted from PPSSPP headless/Headless.cpp.
// Copyright (c) PPSSPP contributors. GPL-2.0-or-later; see docs/ppsspp_ge.md.
// No kernel, memory, GE command, rasterizer or completion function is stubbed here.
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "Common/CommonTypes.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/GPU/GraphicsContext.h"
#include "Core/System.h"
#include "Core/Core.h"

void NativeFrame(GraphicsContext *) {}
void NativeResized() {}
void System_LaunchUrl(LaunchUrlType, std::string_view) {}
std::string System_GetProperty(SystemProperty) { return {}; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty) { return {}; }
int64_t System_GetPropertyInt(SystemProperty) { return -1; }
float System_GetPropertyFloat(SystemProperty) { return -1.0f; }
bool System_GetPropertyBool(SystemProperty p) { return p == SYSPROP_IS_HEADLESS; }
void System_Notify(SystemNotification) {}
void System_PostUIMessage(UIMessage, std::string_view) {}
void System_RunOnMainThread(std::function<void()>) {}
std::vector<std::string> System_GetCameraDeviceList() { return {}; }
void System_AskForPermission(SystemPermission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission) { return PERMISSION_STATUS_DENIED; }
void System_AudioGetDebugStats(char *buf, size_t size) { if (buf && size) buf[0] = 0; }
void System_AudioClear() {}
void System_AudioPushSamples(const s32 *, int, float) {}
bool System_MakeRequest(SystemRequestType, int, const std::string &, const std::string &, int64_t, int64_t) { return false; }
bool NativeSaveSecret(std::string_view, std::string_view) { return false; }
std::string NativeLoadSecret(std::string_view) { return {}; }
void SendDebugOutput(DebugOutputChannel, std::string_view text) { std::fwrite(text.data(), 1, text.size(), stderr); }
void SendDebugScreenshot(const DebugScreenshotDesc &) {}
