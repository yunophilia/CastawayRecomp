#pragma once
// Small host OS services shared by the runtime (Win32 on Windows, POSIX elsewhere).
#include <cstdint>
#include <cstdio>
#include <filesystem>

// Auto-reset event: Set wakes one waiter (or the next Wait).
class HostEvent
{
public:
    HostEvent();
    ~HostEvent();
    HostEvent(const HostEvent&) = delete;
    HostEvent& operator=(const HostEvent&) = delete;
    void Set();
    // Waits up to ms milliseconds; true if the event was set.
    bool Wait(uint32_t ms);
    // Waits up to ns nanoseconds with sub-millisecond precision (Windows: a high-resolution
    // waitable timer, since plain waits follow the ~15.6 ms system tick for throttled processes).
    bool WaitPrecise(uint64_t ns);

private:
    struct Impl;
    Impl* m;
};

enum class HostPriority { BelowNormal, Normal, TimeCritical };
void Host_SetThreadName(const char* name);
void Host_SetThreadPriority(HostPriority p);
// Monotonic high-resolution counter and its frequency (ticks per second)
uint64_t Host_Counter();
uint64_t Host_CounterFreq();
void Host_SleepMs(uint32_t ms);
// Raises the system timer resolution where that matters (Windows' timeBeginPeriod(1)).
void Host_PreciseTimers();

// Files by path (UTF-8 safe on Windows); 64-bit seek
FILE* Host_Open(const std::filesystem::path& p, const char* mode);
int Host_Seek64(FILE* f, int64_t offset, int whence);
// The running executable
std::filesystem::path Host_ExePath();
// Case-insensitive ASCII compare (like strcmp)
int Host_StrICmp(const char* a, const char* b);
// Remembers the command line (Host_Restart starts the game again with it)
void Host_SetArgs(int argc, char** argv);
// Starts a new instance with the remembered command line plus extraArg; true if it started
bool Host_Restart(const char* extraArg);
