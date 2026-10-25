#include "settings_ui.h"
#include "../config.h"
#include "../gx/video.h"
#include "../version.h"
#include "../input/bindings.h"
#include "../input/input.h"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <format>
#include <string>

extern bool g_asyncShaders;
void Renderer_ResetSamplers();

namespace
{
bool s_restartRequested = false;
bool s_askRestart = false;          // open the "needs a restart" prompt next frame
const char* s_help = nullptr;      // description of the hovered setting

// Applies settings that can change while the game runs (called on the render thread).
void ApplyLive()
{
    g_videoConfig.vsync = g_config.vsync;
    g_asyncShaders = g_config.asyncShaders;
    Video_SetFullscreen(g_config.fullscreen);
    Video_SetWindowSize(g_config.windowWidth, g_config.windowHeight);
    Renderer_ResetSamplers();      // anisotropy
    // rumble and pause-on-focus-loss read g_config directly
}

// A setting changed: save it, then apply it live or (in game) offer a restart.
void Changed(SettingsHost host, bool needsRestart)
{
    Config_Save();
    if (host != SettingsHost::InGame) return;
    if (needsRestart) s_askRestart = true;
    else ApplyLive();
}

void Help(const char* text)
{
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) s_help = text;
}

const char* RestartTag(SettingsHost host, bool needsRestart)
{
    return host == SettingsHost::InGame && needsRestart ? " (restart)" : "";
}

bool Check(SettingsHost host, const char* label, bool& v, bool needsRestart, const char* help)
{
    std::string l = std::string(label) + RestartTag(host, needsRestart);
    bool changed = ImGui::Checkbox(l.c_str(), &v);
    Help(help);
    if (changed) Changed(host, needsRestart);
    return changed;
}

template <typename T>
bool Combo(SettingsHost host, const char* label, T& value, const T* values, const char* const* names, int count,
           bool needsRestart, const char* help)
{
    int cur = 0;
    for (int i = 0; i < count; i++)
        if (values[i] == value) cur = i;
    std::string l = std::string(label) + RestartTag(host, needsRestart);
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    if (ImGui::BeginCombo(l.c_str(), names[cur]))
    {
        for (int i = 0; i < count; i++)
            if (ImGui::Selectable(names[i], i == cur) && i != cur)
            {
                value = values[i];
                changed = true;
            }
        ImGui::EndCombo();
    }
    Help(help);
    return changed;   // the caller saves (Changed) once the new value is in g_config
}

void VideoTab(SettingsHost host)
{
    Config& c = g_config;
    ImGui::SeparatorText("Display");
    Check(host, "Fullscreen", c.fullscreen, false, "Borderless fullscreen. F11 or Alt+Enter also toggle it.");
    Check(host, "VSync", c.vsync, false, "Wait for the monitor's refresh. The game paces itself, so this is only needed against tearing.");
    Check(host, "Widescreen (16:9)", c.widescreen, true, "16:9 like the Wii's widescreen setting. Off: 4:3.");
    static const int sizes[][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
    int sizeIdx = 0;
    const int sizeIds[] = { 0, 1, 2, 3, 4 };
    const char* sizeNames[] = { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160" };
    for (int i = 0; i < 5; i++)
        if (sizes[i][0] == c.windowWidth && sizes[i][1] == c.windowHeight) sizeIdx = i;
    if (Combo(host, "Window size", sizeIdx, sizeIds, sizeNames, 5, false, "Size of the window when not fullscreen."))
    {
        c.windowWidth = sizes[sizeIdx][0];
        c.windowHeight = sizes[sizeIdx][1];
        Changed(host, false);
    }

    ImGui::SeparatorText("Rendering");
    const int scales[] = { 1, 2, 3, 4, 5, 6 };
    const char* scaleNames[] = { "1x (640 x 528)", "2x (1280 x 1056)", "3x (1920 x 1584)", "4x (2560 x 2112)", "5x (3200 x 2640)", "6x (3840 x 3168)" };
    if (Combo(host, "Internal resolution", c.internalScale, scales, scaleNames, 6, true, "Resolution the game is rendered at."))
        Changed(host, true);
    const int anis[] = { 1, 2, 4, 8, 16 };
    const char* anisNames[] = { "Off (as the game sets it)", "2x", "4x", "8x", "16x" };
    if (Combo(host, "Anisotropic filtering", c.anisotropy, anis, anisNames, 5, false, "Sharper textures at grazing angles."))
        Changed(host, false);
    Check(host, "Compile shaders in the background", c.asyncShaders, false,
          "Avoids hitches when new effects appear; an object may then show up a frame late.");
    Check(host, "Graphics on a second CPU core", c.gpuThread, true, "Like Dolphin's dual core mode. Leave on unless debugging.");
    Check(host, "HD texture packs", c.texturePack, true,
          "Dolphin-format packs in textures/<game ID> (e.g. R42E69) next to the game, game/textures or Dolphin's Load/Textures.");
}

// Keyboard binding capture: the control waiting for a key, whether it adds to the list,
// and whether every key was released since "Set" was clicked.
int s_keyCapture = -1;
bool s_keyCaptureAdd = false;
bool s_keyCaptureArmed = false;

// The first held key or mouse button that can be bound, or 0.
int HeldBindableKey()
{
    for (int vk = 1; vk < 256; vk++)
        if ((GetAsyncKeyState(vk) & 0x8000) && !Binding_KeyName(vk).empty()) return vk;
    return 0;
}

void BindingsChanged(SettingsHost host)
{
    Input_ReloadBindings();
    Changed(host, false);
}

void InputTab(SettingsHost host)
{
    Config& c = g_config;
    ImGui::SeparatorText("Controller");
    Check(host, "Rumble", c.rumble, false, "Vibrate the controller where the Wii Remote would rumble.");
    ImGui::TextDisabled("Left stick: move the Sim. Right stick: the pointer. Both stick clicks: these settings.");

    if (s_keyCapture >= 0)
    {
        int held = HeldBindableKey();
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) s_keyCapture = -1;
        else if (!s_keyCaptureArmed) s_keyCaptureArmed = held == 0;
        else if (held)
        {
            std::string name = Binding_KeyName(held);
            std::string cur = Binding_Keys(s_keyCapture);
            bool none = _stricmp(cur.c_str(), "NONE") == 0 || cur.empty();
            if (s_keyCaptureAdd && !none)
            {
                bool dup = false;
                for (int vk : Binding_ParseKeys(cur)) dup |= vk == held;
                if (!dup) cur += ", " + name;
            }
            else
                cur = name;
            c.keyBind[s_keyCapture] = cur;
            s_keyCapture = -1;
            BindingsChanged(host);
        }
    }

    ImGui::SeparatorText("Buttons");
    if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Wii", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, 190);
        ImGui::TableSetupColumn("Keyboard and mouse");
        ImGui::TableHeadersRow();
        for (int i = 0; i < kControlCount; i++)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kControls[i].label);

            ImGui::TableNextColumn();
            std::string pad = Binding_Pad(i);
            int cur = kPadButtonCount - 1;
            for (int b = 0; b < kPadButtonCount; b++)
                if (_stricmp(pad.c_str(), kPadButtons[b]) == 0) cur = b;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##pad", kPadButtonLabels[cur]))
            {
                for (int b = 0; b < kPadButtonCount; b++)
                    if (ImGui::Selectable(kPadButtonLabels[b], b == cur))
                    {
                        c.padBind[i] = kPadButtons[b];
                        BindingsChanged(host);
                    }
                ImGui::EndCombo();
            }
            Help("Gamepad button for this Wii button, by position (A is the bottom face button).");

            ImGui::TableNextColumn();
            if (s_keyCapture == i)
                ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "Press a key or mouse button (Esc cancels)");
            else
            {
                if (ImGui::Button("Set")) { s_keyCapture = i; s_keyCaptureAdd = false; s_keyCaptureArmed = false; }
                Help("Replace the keys: click, then press a key or mouse button.");
                ImGui::SameLine();
                if (ImGui::Button("+")) { s_keyCapture = i; s_keyCaptureAdd = true; s_keyCaptureArmed = false; }
                Help("Add another key or mouse button.");
                ImGui::SameLine();
                if (ImGui::Button("Clear")) { c.keyBind[i] = "NONE"; BindingsChanged(host); }
                ImGui::SameLine();
                ImGui::TextUnformatted(Binding_Keys(i).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Restore default buttons"))
    {
        for (int i = 0; i < kControlCount; i++) c.padBind[i].clear(), c.keyBind[i].clear();
        BindingsChanged(host);
    }
    ImGui::TextDisabled("Keyboard: WASD move the Sim, IJKL the pointer, Q / E are the GameCube L / R triggers.");
}

void AudioTab(SettingsHost host)
{
    ImGui::SeparatorText("Audio");
    ImGui::TextDisabled("No audio settings yet.");
}

void SystemTab(SettingsHost host)
{
    Config& c = g_config;
    ImGui::SeparatorText("System");
    const int langs[] = { 1, 2, 3, 4, 5, 6, 0 };
    const char* langNames[] = { "English", "German", "French", "Spanish", "Italian", "Dutch", "Japanese" };
    if (Combo(host, "Language", c.language, langs, langNames, 7, true, "Language of the game's text and menus (the Wii system language)."))
        Changed(host, true);
    Check(host, "Pause when the window is in the background", c.pauseOnFocusLoss, false,
          "Freeze the game while another window has focus.");
    ImGui::Spacing();
    ImGui::TextDisabled("Settings file: %s", Config_Path().c_str());
}
}   // namespace

void SettingsUI_Draw(SettingsHost host)
{
    s_help = nullptr;
    float helpH = ImGui::GetTextLineHeightWithSpacing() * 3;
    if (ImGui::BeginTabBar("settings"))
    {
        struct Tab { const char* name; void (*draw)(SettingsHost); };
        static const Tab tabs[] = { { "Video", VideoTab }, { "Input", InputTab }, { "Audio", AudioTab }, { "System", SystemTab } };
        // test aid: BKR_UI_SHOT_SUBTAB=<tab name> opens that tab first (UI screenshots)
        static const char* shotTab = getenv("BKR_UI_SHOT_SUBTAB");
        static int shotFrames = 0;
        bool pick = shotTab && shotFrames++ < 2;
        for (auto& t : tabs)
            if (ImGui::BeginTabItem(t.name, nullptr, pick && !_stricmp(shotTab, t.name) ? ImGuiTabItemFlags_SetSelected : 0))
            {
                ImGui::BeginChild("page", ImVec2(0, -helpH), ImGuiChildFlags_None);
                t.draw(host);
                static const bool shotBottom = getenv("BKR_UI_SHOT_BOTTOM") != nullptr;   // test aid
                if (shotBottom) ImGui::SetScrollY(ImGui::GetScrollMaxY());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", s_help ? s_help : "");

    if (s_askRestart)
    {
        ImGui::OpenPopup("Restart needed");
        s_askRestart = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Restart needed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This setting needs a restart. Restart the game?");
        ImGui::TextDisabled("Your progress is saved whenever the game saves; unsaved progress is lost.");
        ImGui::Spacing();
        if (ImGui::Button("Restart now"))
        {
            s_restartRequested = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Later")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool SettingsUI_TakeRestartRequest()
{
    bool r = s_restartRequested;
    s_restartRequested = false;
    return r;
}

void SettingsUI_DrawVersion()
{
    ImGui::SameLine();
    float pad = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(g_version).x;
    if (pad > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    ImGui::TextDisabled("%s", g_version);
}

void SettingsUI_LoadFont()
{
#ifdef _WIN32
    const char* windir = getenv("WINDIR");
    std::string base = windir ? windir : "C:\\Windows";
    const char* candidates[] = { "\\Fonts\\segoeui.ttf" };
#else
    std::string base;
    const char* candidates[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                 "/usr/share/fonts/noto/NotoSans-Regular.ttf" };
#endif
    for (const char* c : candidates)
        if (FILE* f = fopen((base + c).c_str(), "rb"))
        {
            fclose(f);
            ImGui::GetIO().Fonts->AddFontFromFileTTF((base + c).c_str(), 18.0f);
            return;
        }
}

void SettingsUI_ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.TabRounding = 4;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = s.Colors;
    const ImVec4 blue(0.12f, 0.44f, 0.92f, 1.00f), blueLight(0.23f, 0.51f, 0.96f, 1.00f),
        blueDim(0.10f, 0.30f, 0.60f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.086f, 0.94f);
    c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.24f, 0.30f, 0.40f, 0.60f);
    c[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.13f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.20f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.25f, 0.40f, 1.00f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.08f, 0.11f, 1.00f);
    c[ImGuiCol_CheckMark] = blue;
    c[ImGuiCol_SliderGrab] = blue;
    c[ImGuiCol_SliderGrabActive] = blueLight;
    c[ImGuiCol_Button] = ImVec4(0.13f, 0.17f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = blueLight;
    c[ImGuiCol_ButtonActive] = blue;
    c[ImGuiCol_Header] = blueDim;
    c[ImGuiCol_HeaderHovered] = blueLight;
    c[ImGuiCol_HeaderActive] = blue;
    c[ImGuiCol_Tab] = ImVec4(0.09f, 0.11f, 0.15f, 1.00f);
    c[ImGuiCol_TabHovered] = blueLight;
    c[ImGuiCol_TabSelected] = blue;
    c[ImGuiCol_TabSelectedOverline] = blueLight;
    c[ImGuiCol_SeparatorHovered] = blueLight;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_NavCursor] = blue;
    c[ImGuiCol_PlotHistogram] = blue;
}
