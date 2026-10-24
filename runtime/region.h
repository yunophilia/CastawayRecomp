#pragma once
// The release being played (USA), picked from the installed disc's game
// ID before the game starts: its recompiled code, its game addresses (game_addrs.h) and
// what the system HLE reports (title ID, save folder, console region).
#include "cpu/ppc_context.h"
#include "install/releases.h"

#include <string>

// Selects the release; false with a message if the ID is not a supported release or this
// build does not contain its code.
bool Region_Select(const std::string& gameId, std::string& error);
const GameRelease& Region_Current();
const PPCRegionCode& Region_Code();
// "/title/00010000/52343245/data"
std::string Region_TitleDataDir();
// The release's TV format is PAL (else NTSC)
inline bool IsPal() { return std::string(Region_Current().video) == "PAL"; }
