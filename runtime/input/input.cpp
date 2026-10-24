// Player input: keyboard and mouse (Win32), gamepads (gamepad.cpp), and scripted input
// for automated test runs.
//
// Keyboard (only while the game window has focus):
//   WASD main stick, IJKL C-stick (camera), arrow keys D-pad,
//   Space A, Backspace B, F X, G Y, Enter START, Q L, E R, Z Z.
// Mouse: Wii Remote pointer; left button Wii A, right button Wii B.
#include "input.h"
#include "gamepad.h"
#include "../config.h"
#include "../gx/video.h"
#include "../log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
constexpr uint16_t PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010,
                   PAD_R = 0x0020, PAD_L = 0x0040, PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800,
                   PAD_START = 0x1000;
constexpr int8_t kStickMax = 100;   // a real GameCube stick reaches about +-100 at its gate

std::mutex s_lock;
bool s_inited = false;
DWORD s_lastPoll = 0;
PadState s_pad{};
PointerState s_pointer{};

// ---------------------------------------------------------------- scripted input
struct ScriptEvent
{
    uint32_t frame;
    std::string action;
    std::vector<std::string> args;
};
std::vector<ScriptEvent> s_script;
size_t s_scriptPos = 0;
bool s_scripted = false;
std::atomic<uint32_t> s_frame{ 0 };
PadState s_scriptPad{};
PointerState s_scriptPointer{};
std::string s_shotDir = ".";   // the working directory (out/ for tools/run_test.ps1)

uint16_t ButtonBit(const std::string& n)
{
    static const struct { const char* name; uint16_t bit; } kNames[] = {
        { "A", PAD_A }, { "B", PAD_B }, { "X", PAD_X }, { "Y", PAD_Y }, { "START", PAD_START }, { "Z", PAD_Z },
        { "L", PAD_L }, { "R", PAD_R }, { "UP", PAD_UP }, { "DOWN", PAD_DOWN }, { "LEFT", PAD_LEFT }, { "RIGHT", PAD_RIGHT },
    };
    for (auto& k : kNames)
        if (_stricmp(n.c_str(), k.name) == 0) return k.bit;
    LOG_WARN("script: unknown button {}", n);
    return 0;
}

int8_t StickArg(const std::string& s) { return (int8_t)std::clamp(atoi(s.c_str()), -127, 127); }

// Applies the events up to the current frame (called with s_lock held).
void RunScript(void (*save)(const char*))
{
    uint32_t frame = s_frame.load();
    while (s_scriptPos < s_script.size() && s_script[s_scriptPos].frame <= frame)
    {
        const ScriptEvent& e = s_script[s_scriptPos++];
        const auto& a = e.args;
        if (e.action == "press")
            for (auto& b : a) s_scriptPad.buttons |= ButtonBit(b);
        else if (e.action == "release")
            for (auto& b : a) s_scriptPad.buttons &= ~ButtonBit(b);
        else if (e.action == "stick" && a.size() == 2)
            s_scriptPad.stickX = StickArg(a[0]), s_scriptPad.stickY = StickArg(a[1]);
        else if (e.action == "cstick" && a.size() == 2)
            s_scriptPad.substickX = StickArg(a[0]), s_scriptPad.substickY = StickArg(a[1]);
        else if (e.action == "trigger" && a.size() == 2)
            s_scriptPad.triggerL = (uint8_t)atoi(a[0].c_str()), s_scriptPad.triggerR = (uint8_t)atoi(a[1].c_str());
        else if (e.action == "point" && a.size() == 2)
            s_scriptPointer.x = (float)atof(a[0].c_str()), s_scriptPointer.y = (float)atof(a[1].c_str()), s_scriptPointer.valid = true;
        else if (e.action == "nopoint")
            s_scriptPointer.valid = false;
        else if (e.action == "mouse" && a.size() == 2)
            (_stricmp(a[0].c_str(), "left") == 0 ? s_scriptPointer.buttonA : s_scriptPointer.buttonB) = _stricmp(a[1].c_str(), "down") == 0;
        else if (e.action == "shot" && save)
        {
            std::string path = s_shotDir + "/" + (a.empty() ? "shot_" + std::to_string(frame) : a[0]) + ".bmp";
            save(path.c_str());
            LOG_INFO("script: frame {} saved {}", frame, path);
        }
        else if (e.action == "log")
        {
            std::string msg;
            for (auto& w : a) msg += w + " ";
            LOG_INFO("script: frame {}: {}", frame, msg);
        }
        else if (e.action == "quit")
        {
            LOG_INFO("script: quit at frame {}", frame);
            fflush(nullptr);
            TerminateProcess(GetCurrentProcess(), 0);
        }
        else
            LOG_WARN("script: unknown action {} at frame {}", e.action, e.frame);
    }
}

// ---------------------------------------------------------------- keyboard and mouse
bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void ReadKeyboardMouse(PadState& pad, PointerState& ptr)
{
    HWND hwnd = (HWND)Video_HWND();
    bool focused = hwnd && GetForegroundWindow() == hwnd;
    ptr = {};
    if (!focused) return;

    static const struct { int vk; uint16_t bit; } kButtons[] = {
        { VK_SPACE, PAD_A }, { VK_BACK, PAD_B }, { 'F', PAD_X }, { 'G', PAD_Y }, { VK_RETURN, PAD_START },
        { 'Q', PAD_L }, { 'E', PAD_R }, { 'Z', PAD_Z }, { VK_UP, PAD_UP }, { VK_DOWN, PAD_DOWN },
        { VK_LEFT, PAD_LEFT }, { VK_RIGHT, PAD_RIGHT },
    };
    for (auto& b : kButtons)
        if (Key(b.vk)) pad.buttons |= b.bit;
    if (Key('Q')) pad.triggerL = 255;
    if (Key('E')) pad.triggerR = 255;
    auto axis = [](bool neg, bool pos) { return (int8_t)((pos ? kStickMax : 0) - (neg ? kStickMax : 0)); };
    int8_t kx = axis(Key('A'), Key('D')), ky = axis(Key('S'), Key('W'));
    if (kx || ky) pad.stickX = kx, pad.stickY = ky;
    int8_t cx = axis(Key('J'), Key('L')), cy = axis(Key('K'), Key('I'));
    if (cx || cy) pad.substickX = cx, pad.substickY = cy;

    POINT pt;
    RECT rc;
    if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt) && GetClientRect(hwnd, &rc) && rc.right > 0 && rc.bottom > 0)
    {
        ptr.valid = pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom;
        ptr.x = std::clamp(2.0f * pt.x / rc.right - 1.0f, -1.0f, 1.0f);
        ptr.y = std::clamp(2.0f * pt.y / rc.bottom - 1.0f, -1.0f, 1.0f);
        ptr.buttonA = ptr.valid && Key(VK_LBUTTON);
        ptr.buttonB = ptr.valid && Key(VK_RBUTTON);
    }
}

void Merge(PadState& into, const PadState& p)
{
    into.buttons |= p.buttons;
    if (p.stickX || p.stickY) into.stickX = p.stickX, into.stickY = p.stickY;
    if (p.substickX || p.substickY) into.substickX = p.substickX, into.substickY = p.substickY;
    into.triggerL = std::max(into.triggerL, p.triggerL);
    into.triggerR = std::max(into.triggerR, p.triggerR);
}
}   // namespace

void Input_Init()
{
    std::lock_guard lk(s_lock);
    if (s_inited) return;
    s_inited = true;
    if (!s_scripted) Gamepad_Init();
}

void Input_Poll()
{
    std::lock_guard lk(s_lock);
    DWORD now = GetTickCount();
    if (s_lastPoll == now) return;
    s_lastPoll = now;
    PadState pad{};
    PointerState ptr{};
    if (s_scripted)
    {
        pad = s_scriptPad;
        ptr = s_scriptPointer;
    }
    else
    {
        ReadKeyboardMouse(pad, ptr);
        PadState g{};
        if (Gamepad_Read(g)) Merge(pad, g);
    }
    pad.connected = true;   // the port always has a controller, so the game never asks for one
    s_pad = pad;
    s_pointer = ptr;
}

PadState Input_Pad()
{
    std::lock_guard lk(s_lock);
    return s_pad;
}

PointerState Input_Pointer()
{
    std::lock_guard lk(s_lock);
    return s_pointer;
}

void Input_SetRumble(bool on)
{
    // never during scripted runs, and only while the game window is in front
    HWND hwnd = (HWND)Video_HWND();
    bool allowed = on && g_config.rumble && !s_scripted && hwnd && GetForegroundWindow() == hwnd;
    Gamepad_Rumble(allowed);
}

void Input_StopRumble() { Gamepad_Rumble(false); }

void Input_LoadScript(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) FATAL("cannot open input script {}", path);
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), f))
    {
        n++;
        if (char* c = strchr(line, '#')) *c = 0;
        std::vector<std::string> words;
        for (char* t = strtok(line, " \t\r\n"); t; t = strtok(nullptr, " \t\r\n")) words.push_back(t);
        if (words.empty()) continue;
        if (words.size() < 2) FATAL("{}:{}: expected <frame> <action> [args]", path, n);
        ScriptEvent e{ (uint32_t)strtoul(words[0].c_str(), nullptr, 10), words[1], { words.begin() + 2, words.end() } };
        s_script.push_back(std::move(e));
    }
    fclose(f);
    std::stable_sort(s_script.begin(), s_script.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    s_scripted = true;
    LOG_INFO("input script {}: {} events", path, s_script.size());
}

bool Input_ScriptActive() { return s_scripted; }

void Input_OnPresent(void (*save)(const char* path))
{
    s_frame++;
    if (!s_scripted) return;
    std::lock_guard lk(s_lock);
    RunScript(save);
}
