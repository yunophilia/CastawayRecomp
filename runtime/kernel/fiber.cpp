#include "fiber.h"
#include "../log.h"

#ifdef _WIN32
#include <windows.h>

namespace
{
struct Start { void (*fn)(void*); void* arg; };
void WINAPI FiberProc(void* p)
{
    Start s = *(Start*)p;
    delete (Start*)p;
    s.fn(s.arg);
}
}   // namespace

HostFiber* Fiber_ConvertThread() { return (HostFiber*)ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH); }

HostFiber* Fiber_Create(size_t stackReserve, void (*fn)(void*), void* arg)
{
    void* f = CreateFiberEx(64 * 1024, stackReserve, FIBER_FLAG_FLOAT_SWITCH, FiberProc, new Start{ fn, arg });
    if (!f) FATAL("CreateFiberEx failed ({})", GetLastError());
    return (HostFiber*)f;
}

void Fiber_Switch(HostFiber* f) { SwitchToFiber(f); }
void Fiber_Delete(HostFiber* f) { DeleteFiber(f); }

#else
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>

struct HostFiber
{
    ucontext_t ctx;
    void* stack = nullptr;
    size_t stackSize = 0;
    void (*fn)(void*) = nullptr;
    void* arg = nullptr;
};

namespace
{
thread_local HostFiber* t_current = nullptr;

void Entry(unsigned lo, unsigned hi)
{
    HostFiber* f = (HostFiber*)(((uintptr_t)hi << 32) | lo);
    f->fn(f->arg);
    FATAL("fiber function returned");
}
}   // namespace

HostFiber* Fiber_ConvertThread()
{
    t_current = new HostFiber();
    return t_current;
}

HostFiber* Fiber_Create(size_t stackReserve, void (*fn)(void*), void* arg)
{
    HostFiber* f = new HostFiber();
    f->stack = mmap(nullptr, stackReserve, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (f->stack == MAP_FAILED) FATAL("fiber stack allocation failed ({})", strerror(errno));
    // guard page at the bottom: an overflow faults here (reported by the crash handler on its
    // alternate stack) instead of running into whatever is mapped below
    mprotect(f->stack, (size_t)sysconf(_SC_PAGESIZE), PROT_NONE);
    f->stackSize = stackReserve;
    f->fn = fn;
    f->arg = arg;
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stackReserve;
    f->ctx.uc_link = nullptr;
    uintptr_t p = (uintptr_t)f;
    makecontext(&f->ctx, (void (*)())Entry, 2, (unsigned)(p & 0xFFFFFFFF), (unsigned)(p >> 32));
    return f;
}

void Fiber_Switch(HostFiber* f)
{
    HostFiber* self = t_current;
    t_current = f;
    swapcontext(&self->ctx, &f->ctx);
}

void Fiber_Delete(HostFiber* f)
{
    if (f->stack) munmap(f->stack, f->stackSize);
    delete f;
}
#endif
