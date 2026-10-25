#pragma once
// Player input: gamepads (any brand, through SDL), keyboard and mouse.
//
// The game reads a GameCube controller on port 4 (its ENgcControllerManager) and Wii
// Remotes through KPAD. Gamepads and the keyboard drive the GameCube controller
// (pad_hle.cpp) with the Wii Remote's buttons, since the game's command maps are written
// for those; the mouse is the pointer (pad_hle.cpp and wpad_hle.cpp).
#include <cstdint>

// Logical buttons, named after the Wii controls the game's commands are mapped to
enum : uint32_t
{
    BTN_A = 1u << 0,
    BTN_B = 1u << 1,
    BTN_C = 1u << 2,       // Nunchuk C
    BTN_Z = 1u << 3,       // Nunchuk Z
    BTN_1 = 1u << 4,
    BTN_2 = 1u << 5,
    BTN_PLUS = 1u << 6,
    BTN_MINUS = 1u << 7,
    BTN_HOME = 1u << 8,
    BTN_UP = 1u << 9,
    BTN_DOWN = 1u << 10,
    BTN_LEFT = 1u << 11,
    BTN_RIGHT = 1u << 12,
};

struct PadState
{
    uint32_t buttons;          // BTN_*
    int8_t stickX, stickY;     // movement stick, -128..127 (up is positive)
    int8_t substickX, substickY;
    uint8_t triggerL, triggerR;
    bool connected;
};

// Wii Remote pointer, in the game's pointer coordinates (0, 0 is the screen centre; the
// screen edges are beyond +-1, see input.cpp)
constexpr float kPointerAway = -1000.0f;   // what the game uses for "pointed away from the screen"
struct PointerState
{
    float x, y;
    bool valid;      // over the game window
};

void Input_Init();
// Called from the PAD/WPAD reads; polls devices at most once per host millisecond.
void Input_Poll();
PadState Input_Pad();
PointerState Input_Pointer();
void Input_SetRumble(bool on);
// The bindings in g_config changed.
void Input_ReloadBindings();
void Input_StopRumble();

// Scripted input for automated tests: --script <file>. While a script runs, real devices
// are ignored and rumble is off.
void Input_LoadScript(const char* path);
bool Input_ScriptActive();
// Called once per presented frame with a function that saves the frame as a BMP.
void Input_OnPresent(void (*save)(const char* path));
