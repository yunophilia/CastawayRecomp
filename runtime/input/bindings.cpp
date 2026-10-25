#include "bindings.h"
#include "input.h"
#include "../config.h"

#include <cstring>
#include <iterator>
#include <sstream>
#include <SDL3/SDL.h>

const Control kControls[kControlCount] = {
    { "a", "A", BTN_A, "a", "SPACE, MOUSE1" },
    { "b", "B", BTN_B, "b", "BACKSPACE, MOUSE2" },
    { "nunchuk_c", "Nunchuk C", BTN_C, "rightshoulder", "C, MOUSE3" },
    { "nunchuk_z", "Nunchuk Z", BTN_Z, "leftshoulder", "Z" },
    { "button_1", "1", BTN_1, "x", "1" },
    { "button_2", "2", BTN_2, "y", "2" },
    { "plus", "+", BTN_PLUS, "start", "ENTER" },
    { "minus", "-", BTN_MINUS, "back", "TAB" },
    { "home", "HOME", BTN_HOME, "guide", "H" },
    { "dpad_up", "D-pad up", BTN_UP, "dpup", "UP" },
    { "dpad_down", "D-pad down", BTN_DOWN, "dpdown", "DOWN" },
    { "dpad_left", "D-pad left", BTN_LEFT, "dpleft", "LEFT" },
    { "dpad_right", "D-pad right", BTN_RIGHT, "dpright", "RIGHT" },
};

const char* const kPadButtons[kPadButtonCount] = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
    "dpup", "dpdown", "dpleft", "dpright", "none",
};
const char* const kPadButtonLabels[kPadButtonCount] = {
    "A (bottom)", "B (right)", "X (left)", "Y (top)", "Back / View", "Guide", "Start / Menu", "Left stick click",
    "Right stick click", "LB", "RB", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "None",
};

namespace
{
struct KeyName { const char* name; int code; };
const KeyName kNamed[] = {
    { "SPACE", SDL_SCANCODE_SPACE }, { "ENTER", SDL_SCANCODE_RETURN }, { "BACKSPACE", SDL_SCANCODE_BACKSPACE },
    { "TAB", SDL_SCANCODE_TAB }, { "LSHIFT", SDL_SCANCODE_LSHIFT }, { "RSHIFT", SDL_SCANCODE_RSHIFT },
    { "LCTRL", SDL_SCANCODE_LCTRL }, { "RCTRL", SDL_SCANCODE_RCTRL }, { "LALT", SDL_SCANCODE_LALT },
    { "RALT", SDL_SCANCODE_RALT }, { "UP", SDL_SCANCODE_UP }, { "DOWN", SDL_SCANCODE_DOWN }, { "LEFT", SDL_SCANCODE_LEFT },
    { "RIGHT", SDL_SCANCODE_RIGHT }, { "INSERT", SDL_SCANCODE_INSERT }, { "DELETE", SDL_SCANCODE_DELETE },
    { "HOME_KEY", SDL_SCANCODE_HOME }, { "END", SDL_SCANCODE_END }, { "PAGEUP", SDL_SCANCODE_PAGEUP },
    { "PAGEDOWN", SDL_SCANCODE_PAGEDOWN }, { "F12", SDL_SCANCODE_F12 },
    { "MOUSE1", kMouseCode + SDL_BUTTON_LEFT }, { "MOUSE2", kMouseCode + SDL_BUTTON_RIGHT },
    { "MOUSE3", kMouseCode + SDL_BUTTON_MIDDLE }, { "MOUSE4", kMouseCode + SDL_BUTTON_X1 },
    { "MOUSE5", kMouseCode + SDL_BUTTON_X2 },
};

std::string Upper(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}   // namespace

int Binding_KeyCode(const std::string& name)
{
    std::string n = Upper(Trim(name));
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return SDL_SCANCODE_A + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return SDL_SCANCODE_1 + (n[0] - '1');
    if (n == "0") return SDL_SCANCODE_0;
    // F1 opens the settings and F11 toggles fullscreen, so they cannot be bound
    if (n.size() >= 2 && n[0] == 'F' && isdigit((unsigned char)n[1]))
    {
        int f = atoi(n.c_str() + 1);
        if (f >= 2 && f <= 10) return SDL_SCANCODE_F1 + f - 1;
    }
    if (n.rfind("NUMPAD", 0) == 0 && n.size() == 7 && isdigit((unsigned char)n[6]))
        return n[6] == '0' ? SDL_SCANCODE_KP_0 : SDL_SCANCODE_KP_1 + (n[6] - '1');
    for (auto& k : kNamed)
        if (n == k.name) return k.code;
    return 0;
}

std::string Binding_KeyName(int code)
{
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return std::string(1, (char)('A' + code - SDL_SCANCODE_A));
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return std::string(1, (char)('1' + code - SDL_SCANCODE_1));
    if (code == SDL_SCANCODE_0) return "0";
    if (code >= SDL_SCANCODE_F2 && code <= SDL_SCANCODE_F10) return "F" + std::to_string(code - SDL_SCANCODE_F1 + 1);
    if (code == SDL_SCANCODE_KP_0) return "NUMPAD0";
    if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) return "NUMPAD" + std::to_string(code - SDL_SCANCODE_KP_1 + 1);
    for (auto& k : kNamed)
        if (code == k.code) return k.name;
    return "";
}

bool Binding_Held(int code)
{
    if (code >= kMouseCode) return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(code - kMouseCode)) != 0;
    int n = 0;
    const bool* keys = SDL_GetKeyboardState(&n);
    return code > 0 && code < n && keys[code];
}

int Binding_HeldAny()
{
    for (int b = SDL_BUTTON_LEFT; b <= SDL_BUTTON_X2; b++)
        if (Binding_Held(kMouseCode + b)) return kMouseCode + b;
    for (int c = 1; c < SDL_SCANCODE_COUNT; c++)
        if (Binding_Held(c) && !Binding_KeyName(c).empty()) return c;
    return 0;
}

std::vector<int> Binding_ParseKeys(const std::string& list)
{
    std::vector<int> out;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ','))
        if (int vk = Binding_KeyCode(item)) out.push_back(vk);
    return out;
}

std::string Binding_Pad(int control)
{
    const std::string& v = g_config.padBind[control];
    return v.empty() ? kControls[control].pad : v;
}

std::string Binding_Keys(int control)
{
    const std::string& v = g_config.keyBind[control];
    return v.empty() ? kControls[control].keys : v;
}
static_assert(kControlCount == std::size(Config{}.padBind));
