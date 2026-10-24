#include "log.h"

#include <cstdlib>
#include <mutex>
#include <share.h>
#include <windows.h>

static std::mutex s_logMutex;
static FILE* s_logFile = nullptr;
static ULONGLONG s_logStart = GetTickCount64();
int g_logLevel = 1;
bool g_noMessageBox = false;

void Log_Open(const char* path)
{
    // shared so the log can be read while the game is running
    s_logFile = _fsopen(path, "w", _SH_DENYWR);
}

void Log_Write(int level, const std::string& s)
{
    if (level < g_logLevel)
        return;
    static const char* kTag[] = { "D", "I", "W", "E" };
    std::lock_guard<std::mutex> lk(s_logMutex);
    fprintf(stdout, "[%s] %s\n", kTag[level & 3], s.c_str());
    if (s_logFile)
    {
        // the file gets a timestamp (seconds since start) for timing analysis
        double t = (GetTickCount64() - s_logStart) / 1000.0;
        fprintf(s_logFile, "[%s %8.3f] %s\n", kTag[level & 3], t, s.c_str());
        fflush(s_logFile);
    }
}

void Fatal(const std::string& s)
{
    Log_Write(3, "FATAL: " + s);
    fflush(stdout);
    if (!g_noMessageBox) MessageBoxA(nullptr, s.c_str(), "CastawayRecomp - fatal error", MB_ICONERROR);
    { void Input_StopRumble(); Input_StopRumble(); }
    ExitProcess(1);
}
