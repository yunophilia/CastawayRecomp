// Player input: keyboard and mouse (Win32), gamepads (gamepad.cpp), and scripted input
// for automated test runs.
//
// Keyboard (only while the game window has focus):
//   WASD movement stick, IJKL pointer, arrow keys D-pad,
//   Space A, Backspace B, C C, Z Z, 1 1, 2 2, Enter +, Tab -, H HOME,
//   Q / E the GameCube L / R triggers.
// Mouse: the pointer (the right stick and IJKL move it too); left button A, right button B,
// middle button C.
#include "input.h"
#include "gamepad.h"
#include "../config.h"
#include "../gx/video.h"
#include "../log.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
constexpr int8_t kStickMax = 100;   // a real GameCube stick reaches about +-100 at its gate

std::mutex s_lock;
bool s_inited = false;
DWORD s_lastPoll = 0;
PadState s_pad{};
PointerState s_pointer{};

// The pointer: the mouse while it moves over the window, else a virtual pointer the right
// stick (or IJKL) pushes around. Screen edges in the game's pointer coordinates follow
// from the calibration in ReadKeyboardMouse.
constexpr float kPtrMinX = (0.0f - 0.488f) / 0.360f, kPtrMaxX = (1.0f - 0.488f) / 0.360f;
constexpr float kPtrMinY = (0.0f - 0.462f) / 0.462f, kPtrMaxY = (1.0f - 0.462f) / 0.462f;
constexpr float kPtrSpeed = 2.4f;   // pointer units per second at full deflection
float s_ptrX = 0, s_ptrY = 0;
POINT s_lastMouse{ -1, -1 };

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

uint32_t ButtonBit(const std::string& n)
{
    static const struct { const char* name; uint32_t bit; } kNames[] = {
        { "A", BTN_A }, { "B", BTN_B }, { "C", BTN_C }, { "Z", BTN_Z }, { "1", BTN_1 }, { "2", BTN_2 },
        { "PLUS", BTN_PLUS }, { "START", BTN_PLUS }, { "MINUS", BTN_MINUS }, { "HOME", BTN_HOME },
        { "UP", BTN_UP }, { "DOWN", BTN_DOWN }, { "LEFT", BTN_LEFT }, { "RIGHT", BTN_RIGHT },
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

    static const struct { int vk; uint32_t bit; } kButtons[] = {
        { VK_SPACE, BTN_A }, { VK_BACK, BTN_B }, { 'C', BTN_C }, { 'Z', BTN_Z }, { '1', BTN_1 }, { '2', BTN_2 },
        { VK_RETURN, BTN_PLUS }, { VK_TAB, BTN_MINUS }, { 'H', BTN_HOME }, { VK_UP, BTN_UP }, { VK_DOWN, BTN_DOWN },
        { VK_LEFT, BTN_LEFT }, { VK_RIGHT, BTN_RIGHT },
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
        // The game's pointer coordinates reach past the screen edges: measured on the main
        // menu, the cursor lands at screen fraction u = 0.488 + 0.360 x, v = 0.462 + 0.462 y.
        bool inside = pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom;
        if (inside && (pt.x != s_lastMouse.x || pt.y != s_lastMouse.y))
        {
            float u = (float)pt.x / rc.right, v = (float)pt.y / rc.bottom;
            s_ptrX = (u - 0.488f) / 0.360f;
            s_ptrY = (v - 0.462f) / 0.462f;
        }
        s_lastMouse = pt;
        if (inside)
        {
            if (Key(VK_LBUTTON)) pad.buttons |= BTN_A;
            if (Key(VK_RBUTTON)) pad.buttons |= BTN_B;
            if (Key(VK_MBUTTON)) pad.buttons |= BTN_C;
        }
    }
    ptr.valid = true;   // the game window has focus
}

// Moves a pointer by the right stick (substick) for dt seconds.
void MovePointer(const PadState& pad, float dt, float& s_ptrX, float& s_ptrY)
{
    float x = pad.substickX / 100.0f, y = -pad.substickY / 100.0f;
    // squared response: slow near the centre for aiming, fast at the edge
    float m = std::sqrt(x * x + y * y);
    if (m < 0.01f) return;
    float k = std::min(m, 1.0f) * kPtrSpeed * dt;   // speed grows with deflection squared (k * x)
    s_ptrX = std::clamp(s_ptrX + x * k, kPtrMinX, kPtrMaxX);
    s_ptrY = std::clamp(s_ptrY + y * k, kPtrMinY, kPtrMaxY);
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
        // scripts step in whole frames: the right stick moves the pointer by 1/60 s per poll
        // that sees a new frame
        static uint32_t lastFrame = 0;
        if (s_frame.load() != lastFrame)
        {
            lastFrame = s_frame.load();
            if (s_scriptPad.substickX || s_scriptPad.substickY)
            {
                s_scriptPointer.valid = true;
                MovePointer(s_scriptPad, 1.0f / 60, s_scriptPointer.x, s_scriptPointer.y);
            }
        }
        pad = s_scriptPad;
        pad.substickX = pad.substickY = 0;
        ptr = s_scriptPointer;
    }
    else
    {
        static DWORD last = now;
        float dt = std::min(0.1f, (now - last) / 1000.0f);
        last = now;
        ReadKeyboardMouse(pad, ptr);
        PadState g{};
        if (Gamepad_Read(g)) Merge(pad, g);
        // the Wii game has no C-stick: the right stick is the pointer
        if (ptr.valid) MovePointer(pad, dt, s_ptrX, s_ptrY);
        pad.substickX = pad.substickY = 0;
        ptr.x = s_ptrX;
        ptr.y = s_ptrY;
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
