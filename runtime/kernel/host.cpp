#include "host.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#include <cwchar>
#include <string>

struct HostEvent::Impl
{
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
};

HostEvent::HostEvent() : m(new Impl) {}
HostEvent::~HostEvent()
{
    CloseHandle(m->ev);
    if (m->timer) CloseHandle(m->timer);
    delete m;
}
void HostEvent::Set() { SetEvent(m->ev); }
bool HostEvent::Wait(uint32_t ms) { return WaitForSingleObject(m->ev, ms) == WAIT_OBJECT_0; }

bool HostEvent::WaitPrecise(uint64_t ns)
{
    if (!m->timer) return Wait((uint32_t)std::max<uint64_t>(1, ns / 1000000));
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)std::max<uint64_t>(1, ns / 100);   // relative, 100 ns units
    SetWaitableTimerEx(m->timer, &due, 0, nullptr, nullptr, nullptr, 0);
    HANDLE hs[2] = { m->ev, m->timer };
    DWORD r = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
    CancelWaitableTimer(m->timer);
    return r == WAIT_OBJECT_0;
}

void Host_SetThreadName(const char* name)
{
    wchar_t w[64];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64);
    SetThreadDescription(GetCurrentThread(), w);
}

void Host_SetThreadPriority(HostPriority p)
{
    int v = p == HostPriority::TimeCritical ? THREAD_PRIORITY_TIME_CRITICAL
          : p == HostPriority::BelowNormal  ? THREAD_PRIORITY_BELOW_NORMAL
                                            : THREAD_PRIORITY_NORMAL;
    SetThreadPriority(GetCurrentThread(), v);
}

uint64_t Host_Counter()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart;
}

uint64_t Host_CounterFreq()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (uint64_t)f.QuadPart;
}

void Host_SleepMs(uint32_t ms) { Sleep(ms); }
void Host_PreciseTimers() { timeBeginPeriod(1); }

FILE* Host_Open(const std::filesystem::path& p, const char* mode)
{
    wchar_t wm[8] = {};
    for (int i = 0; i < 7 && mode[i]; i++) wm[i] = (wchar_t)mode[i];
    return _wfopen(p.c_str(), wm);
}

int Host_Seek64(FILE* f, int64_t offset, int whence) { return _fseeki64(f, offset, whence); }

std::filesystem::path Host_ExePath()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

int Host_StrICmp(const char* a, const char* b) { return _stricmp(a, b); }

void Host_SetArgs(int, char**) {}   // Windows keeps the command line itself

bool Host_Restart(const char* extraArg)
{
    std::wstring cmd = GetCommandLineW();
    if (extraArg && !wcsstr(cmd.c_str(), std::filesystem::path(extraArg).c_str()))
        cmd += L" " + std::filesystem::path(extraArg).wstring();
    std::wstring exe = Host_ExePath().wstring();
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>
#include <string>
#include <vector>

struct HostEvent::Impl
{
    std::mutex mu;
    std::condition_variable cv;
    bool set = false;
};

HostEvent::HostEvent() : m(new Impl) {}
HostEvent::~HostEvent() { delete m; }

void HostEvent::Set()
{
    {
        std::lock_guard lk(m->mu);
        m->set = true;
    }
    m->cv.notify_one();
}

bool HostEvent::Wait(uint32_t ms) { return WaitPrecise((uint64_t)ms * 1000000); }

bool HostEvent::WaitPrecise(uint64_t ns)
{
    std::unique_lock lk(m->mu);
    bool got = m->cv.wait_for(lk, std::chrono::nanoseconds(ns), [&] { return m->set; });
    m->set = false;
    return got;
}

void Host_SetThreadName(const char* name)
{
    char n[16];   // Linux limit, with the terminator
    snprintf(n, sizeof(n), "%s", name);
    pthread_setname_np(pthread_self(), n);
}

void Host_SetThreadPriority(HostPriority p)
{
    // without privileges only a lower priority can be set; that is enough for below-normal
    if (p == HostPriority::BelowNormal) setpriority(PRIO_PROCESS, 0, 5);
}

uint64_t Host_Counter()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

uint64_t Host_CounterFreq() { return 1000000000ull; }
void Host_SleepMs(uint32_t ms) { usleep(ms * 1000); }
void Host_PreciseTimers() {}
FILE* Host_Open(const std::filesystem::path& p, const char* mode) { return fopen(p.c_str(), mode); }
int Host_Seek64(FILE* f, int64_t offset, int whence) { return fseeko(f, (off_t)offset, whence); }

std::filesystem::path Host_ExePath()
{
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}

int Host_StrICmp(const char* a, const char* b) { return strcasecmp(a, b); }

static std::vector<std::string> s_args;
void Host_SetArgs(int argc, char** argv) { s_args.assign(argv, argv + argc); }

bool Host_Restart(const char* extraArg)
{
    std::vector<std::string> args = s_args;
    if (extraArg && std::find(args.begin(), args.end(), extraArg) == args.end()) args.push_back(extraArg);
    std::string exe = Host_ExePath().string();
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        std::vector<char*> av;
        for (auto& a : args) av.push_back(a.data());
        av.push_back(nullptr);
        execv(exe.c_str(), av.data());
        _exit(127);
    }
    return true;
}
#endif
