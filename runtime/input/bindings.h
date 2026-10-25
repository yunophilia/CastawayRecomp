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
// NUMPAD0-NUMPAD9, MOUSE1-MOUSE5, NONE. Keys are by position (SDL scancodes: Z is the key
// where a US keyboard has Z).
// Codes: an SDL_Scancode, or kMouseCode + an SDL mouse button (1 left .. 5).
constexpr int kMouseCode = 1000;
int Binding_KeyCode(const std::string& name);    // 0 if unknown
std::string Binding_KeyName(int code);            // "" if it cannot be bound
std::vector<int> Binding_ParseKeys(const std::string& list);
// The key or mouse button is held (keyboard state from SDL's event loop on the window thread).
bool Binding_Held(int code);
// The first held key or mouse button that can be bound, or 0 (binding capture).
int Binding_HeldAny();

// The bindings in effect (config.ini values, or the defaults where a value is empty)
std::string Binding_Pad(int control);
std::string Binding_Keys(int control);
