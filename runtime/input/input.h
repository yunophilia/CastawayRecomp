#pragma once
// Player input: gamepads (any brand, through SDL), keyboard and mouse.
//
// The game reads a GameCube controller on port 4 (its ENgcControllerManager) and Wii
// Remotes through KPAD. Gamepads and the keyboard drive the GameCube controller (hw/si.cpp);
// the mouse drives the Wii Remote's IR pointer (input/wpad_hle.cpp).
#include <cstdint>

// GameCube controller state, in PADStatus terms
struct PadState
{
    uint16_t buttons;          // PAD_BUTTON_* bits (A 0x100, B 0x200, X 0x400, Y 0x800, START 0x1000,
                               // Z 0x10, R 0x20, L 0x40, D-pad left/right/down/up 1/2/4/8)
    int8_t stickX, stickY;     // main stick, -128..127 (up is positive)
    int8_t substickX, substickY;
    uint8_t triggerL, triggerR;
    bool connected;
};

// Wii Remote pointer, in KPAD's -1..1 screen coordinates
struct PointerState
{
    float x, y;
    bool valid;      // over the game window
    bool buttonA;    // left mouse button
    bool buttonB;    // right mouse button
};

void Input_Init();
// Called once per guest frame (from the PAD/WPAD reads); polls devices at most once per host ms.
void Input_Poll();
PadState Input_Pad();
PointerState Input_Pointer();
void Input_SetRumble(bool on);
void Input_StopRumble();

// Scripted input for automated tests: --script <file>. While a script runs, real devices
// are ignored and rumble is off.
void Input_LoadScript(const char* path);
bool Input_ScriptActive();
// Called once per presented frame with a function that saves the frame as a BMP.
void Input_OnPresent(void (*save)(const char* path));
