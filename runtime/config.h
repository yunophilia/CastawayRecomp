#pragma once
#include <string>

// User settings loaded from config.ini next to the executable (created with
// defaults on first run). Command line options override them.
struct Config
{
    // [Video]
    int internalScale = 3;       // EFB resolution multiplier (1 = 640x528)
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = false;
    bool widescreen = true;
    int anisotropy = 1;          // forced anisotropic filtering (1 = as the game sets it)
    bool asyncShaders = true;    // compile new shaders in the background (no hitches)
    bool gpuThread = true;       // GX processing/rendering on its own thread
    bool texturePack = true;     // load Dolphin-format custom textures if present
    std::string texturePackDir;  // empty: <exe>/textures, game/textures, Dolphin's Load/Textures
    // [System]
    int language = 1;                // Wii IPL.LNG: 0 ja, 1 en, 2 de, 3 fr, 4 es, 5 it, 6 nl
    bool pauseOnFocusLoss = false;   // freeze the game while the window is in the background
    // [Input]
    bool rumble = true;
    // [Gamepad] / [Keyboard]: per game button (input/bindings.cpp kControls), a gamepad button
    // name and a comma-separated key list; empty means the default
    std::string padBind[13];
    std::string keyBind[13];
    // [Paths]
    std::string dataDir;
    std::string isoPath;
};

extern Config g_config;


// Loads config.ini (writing the defaults if it does not exist) and remembers its path.
void Config_Load(const std::string& path);
// Writes the current g_config back to config.ini (with comments).
bool Config_Save();
const std::string& Config_Path();
std::string Config_Text(const Config& c);
