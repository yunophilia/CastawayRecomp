#pragma once
// Gamepads of any brand through SDL3 (Xbox, PlayStation, Switch Pro, generic), mapped
// onto the GameCube controller by position:
//   south/east/west/north face buttons -> A/B/X/Y, Start -> START, LB/RB -> Z,
//   LT/RT -> L/R (analog), D-pad -> D-pad, left stick -> main stick, right stick -> C-stick.
// The first connected gamepad that is pressed becomes the active one.
#include "input.h"

void Gamepad_Init();
// Reads the active gamepad (false if none, or the game window is not in front).
bool Gamepad_Read(PadState& out);
void Gamepad_Rumble(bool on);
