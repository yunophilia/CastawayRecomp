#pragma once
// Gamepads of any brand through SDL3 (Xbox, PlayStation, Switch Pro, generic), by position:
//   south/east face buttons -> A/B, west/north -> 1/2, LB/RB -> Nunchuk Z/C,
//   Back/Start -> -/+, Guide (or Back + Start) -> HOME, D-pad -> D-pad,
//   left stick -> movement, right stick -> pointer, LT/RT -> GameCube L/R (analog).
// The first connected gamepad that is pressed becomes the active one.
#include "input.h"

void Gamepad_Init();
// Reads the active gamepad (false if none, or the game window is not in front).
bool Gamepad_Read(PadState& out);
void Gamepad_Rumble(bool on);
