#include "gamepad.h"
#include "../gx/video.h"
#include "../log.h"
#include "../ui/overlay.h"
#include "bindings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
constexpr float kDeadzone = 0.15f;
constexpr int kStickMax = 100;          // GameCube stick gate

bool s_ok = false;
DWORD s_sdlThread = 0;                 // SDL is only ever called from this thread (the game's CPU thread)
std::atomic<bool> s_stopRequested{ false };
SDL_Gamepad* s_active = nullptr;
bool s_rumbling = false;
std::atomic<bool> s_reload{ true };                               // rebuild s_buttons from the bindings
std::vector<std::pair<SDL_GamepadButton, uint32_t>> s_buttons;   // button -> BTN_*
DWORD s_rumbleSent = 0;
constexpr Uint32 kRumbleMs = 2000;   // the game switches the motor off itself; renewed while it stays on

// -1..1 stick pair with a radial deadzone, scaled to the GameCube range
void Stick(SDL_Gamepad* g, SDL_GamepadAxis ax, SDL_GamepadAxis ay, int8_t& ox, int8_t& oy)
{
    float x = SDL_GetGamepadAxis(g, ax) / 32767.0f, y = -SDL_GetGamepadAxis(g, ay) / 32767.0f;
    float m = std::sqrt(x * x + y * y);
    if (m < kDeadzone)
    {
        ox = oy = 0;
        return;
    }
    float s = std::min(1.0f, (m - kDeadzone) / (1.0f - kDeadzone)) / m;
    ox = (int8_t)std::lround(std::clamp(x * s, -1.0f, 1.0f) * kStickMax);
    oy = (int8_t)std::lround(std::clamp(y * s, -1.0f, 1.0f) * kStickMax);
}

uint8_t Trigger(SDL_Gamepad* g, SDL_GamepadAxis a)
{
    int v = SDL_GetGamepadAxis(g, a);   // 0..32767
    return (uint8_t)std::clamp(v * 255 / 32767, 0, 255);
}

bool AnyInput(SDL_Gamepad* g)
{
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (SDL_GetGamepadButton(g, (SDL_GamepadButton)b)) return true;
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
        if (std::abs(SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a)) > 16000) return true;
    return false;
}

// Opens newly connected gamepads, closes removed ones, and picks the active one.
void Refresh()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED)
        {
            if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which))
            {
                LOG_INFO("gamepad connected: {}", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?");
                if (!s_active) s_active = g;
            }
        }
        else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            SDL_Gamepad* g = SDL_GetGamepadFromID(ev.gdevice.which);
            if (g == s_active) s_active = nullptr;
            if (g) SDL_CloseGamepad(g);
            LOG_INFO("gamepad disconnected");
        }
    }
    // switch to whichever pad was used last
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n))
    {
        for (int i = 0; i < n; i++)
        {
            SDL_Gamepad* g = SDL_GetGamepadFromID(ids[i]);
            if (g && g != s_active && AnyInput(g))
            {
                if (s_active && s_rumbling) SDL_RumbleGamepad(s_active, 0, 0, 0);
                s_active = g;
                break;
            }
        }
        if (!s_active && n) s_active = SDL_GetGamepadFromID(ids[0]);
        SDL_free(ids);
    }
}

bool Focused()
{
    HWND hwnd = (HWND)Video_HWND();
    return hwnd && GetForegroundWindow() == hwnd;
}
}   // namespace

void Gamepad_Init()
{
    // input is read on the game's CPU thread; we check the window focus ourselves
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        LOG_WARN("SDL gamepad init failed: {}; gamepads are disabled", SDL_GetError());
        return;
    }
    s_ok = true;
    s_sdlThread = GetCurrentThreadId();
    LOG_INFO("SDL {}.{}.{} gamepad support", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
}

bool Gamepad_Read(PadState& p)
{
    if (!s_ok || GetCurrentThreadId() != s_sdlThread) return false;
    if (s_stopRequested.exchange(false) && s_rumbling) Gamepad_Rumble(false);
    Refresh();
    if (!s_active) return false;
    if (!Focused())
    {
        if (s_rumbling) Gamepad_Rumble(false);
        return false;
    }
    SDL_Gamepad* g = s_active;
    if (s_rumbling && GetTickCount() - s_rumbleSent > kRumbleMs / 2)
    {
        s_rumbleSent = GetTickCount();
        SDL_RumbleGamepad(g, 0xC000, 0x8000, kRumbleMs);
    }
    if (s_reload.exchange(false))
    {
        s_buttons.clear();
        for (int i = 0; i < kControlCount; i++)
        {
            SDL_GamepadButton b = SDL_GetGamepadButtonFromString(Binding_Pad(i).c_str());
            if (b != SDL_GAMEPAD_BUTTON_INVALID) s_buttons.push_back({ b, kControls[i].bit });
        }
    }
    p = {};
    for (auto& [b, bit] : s_buttons)
        if (SDL_GetGamepadButton(g, b)) p.buttons |= bit;
    Stick(g, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, p.stickX, p.stickY);
    Stick(g, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, p.substickX, p.substickY);
    p.triggerL = Trigger(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    p.triggerR = Trigger(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    // both stick clicks open/close the settings overlay
    static bool bothSticks = false;
    bool sticks = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    if (sticks && !bothSticks) Overlay_Toggle();
    bothSticks = sticks;
    // Back + Start together is HOME too (pads without a usable guide button)
    if ((p.buttons & (BTN_MINUS | BTN_PLUS)) == (BTN_MINUS | BTN_PLUS)) p.buttons = (p.buttons & ~(BTN_MINUS | BTN_PLUS)) | BTN_HOME;
    return true;
}

void Gamepad_ReloadBindings() { s_reload = true; }

void Gamepad_Rumble(bool on)
{
    if (!s_ok) return;
    if (GetCurrentThreadId() != s_sdlThread)
    {
        // other threads (window focus, crash and exit paths) only ask; the effect also
        // runs out by itself within kRumbleMs
        if (!on) s_stopRequested = true;
        return;
    }
    if (!s_active || on == s_rumbling) return;
    s_rumbling = on;
    s_rumbleSent = GetTickCount();
    SDL_RumbleGamepad(s_active, on ? 0xC000 : 0, on ? 0x8000 : 0, on ? kRumbleMs : 0);
}
