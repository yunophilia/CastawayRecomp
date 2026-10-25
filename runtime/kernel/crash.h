#pragma once
// Crash reporting and diagnostics (crash.cpp shared, crash_win32.cpp / crash_posix.cpp per platform).
#include <cstdint>
#include <string>

void Crash_Init();
void Crash_BuildMap();
// The recompiled guest function a host code address belongs to (0 if none)
uint32_t Crash_GuestFunctionForHost(uintptr_t hostAddr);
void Crash_DumpThreads();
void Crash_LogStatus(uint32_t lastFrames);
void Crash_LogFreeze(const std::string& samples);
void Crash_StartWatchdog();
void Crash_SetWatch(uint32_t addr);
void Crash_SampleSlowGuest();
void Crash_StartProfiler(int startSec, int durationSec);
