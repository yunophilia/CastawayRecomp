#pragma once
#include <filesystem>
#include <functional>
#include <string>

struct SDL_Window;

// Draws one frame of install progress (fraction 0..1); returns false when the player cancels.
using InstallProgressFn = std::function<bool(const char* line, double fraction)>;

// Asks for the disc image and installs the game data into dest; true once installed.
// foundImage: a disc image found next to the game, offered first. damage: why the existing
// install needs replacing (empty when nothing is installed yet).
bool InstallUI_Run(SDL_Window* parent, const std::filesystem::path& dest, const std::string& foundImage,
                   const std::string& damage, const InstallProgressFn& draw);
