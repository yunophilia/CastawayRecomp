#pragma once
// Gamepads of any brand through SDL3 (Xbox, PlayStation, Switch Pro, generic). Buttons are
// bound by position in config.ini [Gamepad] (input/bindings.cpp has the defaults); Back +
// Start together is HOME too. Left stick -> movement, right stick -> pointer, LT/RT ->
// GameCube L/R (analog); both stick clicks open the settings overlay.
// The first connected gamepad that is pressed becomes the active one.
#include "input.h"

void Gamepad_Init();
// Reads the active gamepad (false if none, or the game window is not in front).
bool Gamepad_Read(PadState& out);
void Gamepad_Rumble(bool on);
// The bindings changed (config.ini [Gamepad]); applied on the next read.
void Gamepad_ReloadBindings();
