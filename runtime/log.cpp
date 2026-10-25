#include "log.h"

#include <cstdlib>
#include <mutex>
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <share.h>
#endif

static std::mutex s_logMutex;
static FILE* s_logFile = nullptr;
static const uint64_t s_logStart = SDL_GetTicks();
int g_logLevel = 1;
bool g_noMessageBox = false;

void Log_Open(const char* path)
{
#ifdef _WIN32
    // shared so the log can be read while the game is running
    s_logFile = _fsopen(path, "w", _SH_DENYWR);
#else
    s_logFile = fopen(path, "w");
#endif
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
        double t = (SDL_GetTicks() - s_logStart) / 1000.0;
        fprintf(s_logFile, "[%s %8.3f] %s\n", kTag[level & 3], t, s.c_str());
        fflush(s_logFile);
    }
}

void Fatal(const std::string& s)
{
    Log_Write(3, "FATAL: " + s);
    fflush(stdout);
    if (!g_noMessageBox) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CastawayRecomp - fatal error", s.c_str(), nullptr);
    { void Input_StopRumble(); Input_StopRumble(); }
    _Exit(1);
}
