#include "bindings.h"
#include "input.h"
#include "../config.h"

#include <cstring>
#include <iterator>
#include <sstream>
#include <windows.h>

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
struct KeyName { const char* name; int vk; };
const KeyName kNamed[] = {
    { "SPACE", VK_SPACE }, { "ENTER", VK_RETURN }, { "BACKSPACE", VK_BACK }, { "TAB", VK_TAB },
    { "LSHIFT", VK_LSHIFT }, { "RSHIFT", VK_RSHIFT }, { "LCTRL", VK_LCONTROL }, { "RCTRL", VK_RCONTROL },
    { "LALT", VK_LMENU }, { "RALT", VK_RMENU }, { "UP", VK_UP }, { "DOWN", VK_DOWN }, { "LEFT", VK_LEFT },
    { "RIGHT", VK_RIGHT }, { "INSERT", VK_INSERT }, { "DELETE", VK_DELETE }, { "HOME_KEY", VK_HOME },
    { "END", VK_END }, { "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "MOUSE1", VK_LBUTTON },
    { "MOUSE2", VK_RBUTTON }, { "MOUSE3", VK_MBUTTON }, { "MOUSE4", VK_XBUTTON1 }, { "MOUSE5", VK_XBUTTON2 },
    { "F12", VK_F12 },
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
    if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
    // F1 opens the settings and F11 toggles fullscreen, so they cannot be bound
    if (n.size() >= 2 && n[0] == 'F' && isdigit((unsigned char)n[1]))
    {
        int f = atoi(n.c_str() + 1);
        if (f >= 2 && f <= 10) return VK_F1 + f - 1;
    }
    if (n.rfind("NUMPAD", 0) == 0 && n.size() == 7 && isdigit((unsigned char)n[6])) return VK_NUMPAD0 + (n[6] - '0');
    for (auto& k : kNamed)
        if (n == k.name) return k.vk;
    return 0;
}

std::string Binding_KeyName(int vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= VK_F2 && vk <= VK_F10) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "NUMPAD" + std::to_string(vk - VK_NUMPAD0);
    for (auto& k : kNamed)
        if (vk == k.vk) return k.name;
    return "";
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
