#pragma once
// Host fibers for guest threads: Win32 fibers on Windows, ucontext elsewhere.
#include <cstddef>

struct HostFiber;

// Turns the calling thread into a fiber (once, before any other fiber call).
HostFiber* Fiber_ConvertThread();
// A fiber that runs fn(arg) on its own stack (reserved, committed as it grows).
HostFiber* Fiber_Create(size_t stackReserve, void (*fn)(void*), void* arg);
// Switches from the current fiber to f.
void Fiber_Switch(HostFiber* f);
void Fiber_Delete(HostFiber* f);
