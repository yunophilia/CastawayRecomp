#pragma once
// Remappable controls: each of the game's (Wii) buttons gets a gamepad button and a list of
// keyboard keys / mouse buttons, stored in config.ini ([Gamepad] and [Keyboard]).
#include <cstdint>
#include <string>
#include <vector>

struct Control
{
    const char* key;        // config.ini name
    const char* label;      // settings UI
    uint32_t bit;           // BTN_*
    const char* pad;        // default gamepad button (SDL name)
    const char* keys;       // default keys, comma-separated
};

constexpr int kControlCount = 13;
extern const Control kControls[kControlCount];

// Gamepad buttons that can be bound, as SDL names and as shown in the settings
constexpr int kPadButtonCount = 16;
extern const char* const kPadButtons[kPadButtonCount];        // "a", "b", ..., "none"
extern const char* const kPadButtonLabels[kPadButtonCount];

// Key names: A-Z, 0-9, F2-F10, F12, SPACE, ENTER, BACKSPACE, TAB, LSHIFT, RSHIFT, LCTRL,
// RCTRL, LALT, RALT, UP, DOWN, LEFT, RIGHT, INSERT, DELETE, HOME_KEY, END, PAGEUP, PAGEDOWN,
// NUMPAD0-NUMPAD9, MOUSE1-MOUSE5, NONE.
int Binding_KeyCode(const std::string& name);    // Windows virtual-key code, 0 if unknown
std::string Binding_KeyName(int vk);              // "" if the key cannot be bound
std::vector<int> Binding_ParseKeys(const std::string& list);

// The bindings in effect (config.ini values, or the defaults where a value is empty)
std::string Binding_Pad(int control);
std::string Binding_Keys(int control);
