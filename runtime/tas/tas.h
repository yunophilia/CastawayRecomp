#pragma once
// Test automation ("TAS" scripts, --tas <file>): sequential controller input timed by game
// time (video fields), with waits on game state, checks, screenshots, memory dumps and
// telemetry. Everything here only feeds controller input or reads guest memory; game code
// is never changed. See docs/tas.md for the script language.
#include <cstdint>
#include <string>

struct PadState;
struct PointerState;

void Tas_Load(const char* path);
void Tas_SetTelemetry(const char* csvPath);
bool Tas_Active();
// Each video field (input.cpp, with the input lock held): runs the script and fills the
// controller and pointer the game reads until the next field.
void Tas_OnField(PadState& pad, PointerState& pointer);
// Once per presented frame (GX thread): screenshots, telemetry rows, quitting.
void Tas_OnPresent(void (*saveShot)(const char* path));
// Disc file reads ("until file <name>") and game state machine transitions
// ("until state <class>"): a state machine (its StateMachineStatus) and the vtable of the
// state it enters (0: none).
void Tas_OnFileRead(const std::string& path);
void Tas_OnState(uint32_t machine, uint32_t vtable);
