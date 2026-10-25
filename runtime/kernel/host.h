#pragma once
// Small host OS services shared by the runtime (Win32 on Windows, POSIX elsewhere).
#include <cstdint>

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
