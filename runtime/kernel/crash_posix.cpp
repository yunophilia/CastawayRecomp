// Crash reporting on POSIX systems: fatal signals are logged with the guest function and
// a host backtrace. The watchdog logs the status line and freezes like on Windows, but
// without sampling where the game thread is (no thread suspension here).
#include "crash.h"
#include "host.h"
#include "../log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <thread>
#include <unistd.h>

extern bool g_noMessageBox;
extern uint32_t g_framesPresented;
bool g_profileGX = false;   // --profile-gx (Windows only for now)

namespace
{
void WriteRaw(const char* s) { (void)!write(STDERR_FILENO, s, strlen(s)); }

void OnFatalSignal(int sig, siginfo_t* info, void* uctx)
{
    static std::atomic<bool> inHandler{ false };
    if (inHandler.exchange(true)) _exit(3);
    uintptr_t pc = 0;
#if defined(__x86_64__)
    pc = (uintptr_t)((ucontext_t*)uctx)->uc_mcontext.gregs[REG_RIP];
#endif
    // not async-signal-safe, but the process is going down anyway
    LOG_ERROR("=== fatal signal {} ({}) at pc={:x} (fault addr {}) ===", sig, strsignal(sig), pc, info->si_addr);
    LOG_ERROR("  in guest function ~fn_{:08X}", Crash_GuestFunctionForHost(pc));
    void* frames[64];
    int n = backtrace(frames, 64);
    if (char** names = backtrace_symbols(frames, n))
    {
        for (int i = 0; i < n; i++) LOG_ERROR("  host frame {:2}: {}", i, names[i]);
        free(names);
    }
    else
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    if (!g_noMessageBox) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CastawayRecomp", "The game crashed. See CastawayRecomp.log for details.", nullptr);
    WriteRaw("CastawayRecomp crashed\n");
    _exit(2);
}
}   // namespace

void Crash_Init()
{
    Crash_BuildMap();
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnFatalSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    // the handler runs on its own stack, so a guest stack overflow is reported too
    static uint8_t altStack[256 * 1024];
    stack_t ss{ altStack, 0, sizeof(altStack) };
    sigaltstack(&ss, nullptr);
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE }) sigaction(sig, &sa, nullptr);
}

void Crash_StartWatchdog()
{
    std::thread([] {
        Host_SetThreadName("watchdog");
        uint32_t lastFrames = 0;
        for (;;)
        {
            Host_SleepMs(3000);
            Crash_LogStatus(lastFrames);
            if (g_framesPresented == lastFrames) Crash_LogFreeze("(not sampled on this platform)");
            lastFrames = g_framesPresented;
        }
    }).detach();
}

void Crash_SetWatch(uint32_t addr) { LOG_WARN("--watch {:08x}: write watches are only available on Windows", addr); }
void Crash_SampleSlowGuest() {}
void Crash_StartProfiler(int, int) { LOG_WARN("--profile: the sampling profiler is only available on Windows"); }
