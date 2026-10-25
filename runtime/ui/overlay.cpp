#include "overlay.h"
#include "settings_ui.h"
#include "ui_shot.h"
#include "../log.h"
#include "../input/input.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

void App_Restart();
void Input_StopRumble();

namespace
{
SDL_Window* s_window = nullptr;
ID3D11Device* s_dev = nullptr;
ID3D11DeviceContext* s_ctx = nullptr;
bool s_ready = false;
std::atomic<bool> s_open{ false };
std::atomic<bool> s_homePress{ false };
std::mutex s_msgMutex;
std::vector<SDL_Event> s_msgs;     // input events, replayed on the render thread
std::deque<std::string> s_texts;   // text of the queued text-input events (SDL owns the original)
// test aid: BKR_UI_SHOT_OVERLAY=<file.bmp> opens the overlay BKR_UI_SHOT_AFTER seconds
// (default 30) into the game, saves a screenshot a second later and closes it again
const char* s_shotPath = nullptr;
uint64_t s_shotOpenAt = 0;

bool IsInput(const SDL_Event& e)
{
    switch (e.type)
    {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_WINDOW_FOCUS_GAINED: case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return true;
    default:
        return false;
    }
}

}   // namespace

void Overlay_Init(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    s_shotPath = getenv("BKR_UI_SHOT_OVERLAY");
    if (Input_ScriptActive() && !s_shotPath) return;   // scripted runs drive the game; no overlay
    if (s_shotPath)
    {
        const char* after = getenv("BKR_UI_SHOT_AFTER");
        s_shotOpenAt = SDL_GetTicks() + (after ? atoi(after) : 30) * 1000ull;
    }
    s_window = window;
    s_dev = device;
    s_ctx = ctx;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing to remember between runs
    // the window thread shows and hides the system cursor (with the overlay)
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    SettingsUI_LoadFont();
    SettingsUI_ApplyStyle();
    ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX11_Init(device, ctx);
    s_ready = true;
}

bool Overlay_HandleEvent(const SDL_Event& e)
{
    if (!s_ready || !IsInput(e)) return false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        (e.key.scancode == SDL_SCANCODE_F1 || (e.key.scancode == SDL_SCANCODE_ESCAPE && s_open)))
    {
        Overlay_Toggle();
        return true;
    }
    if (!s_open) return false;
    std::lock_guard lk(s_msgMutex);
    s_msgs.push_back(e);
    if (e.type == SDL_EVENT_TEXT_INPUT) s_texts.emplace_back(e.text.text ? e.text.text : "");
    // the game sees no keys or clicks while open; focus changes still reach the window
    return e.type != SDL_EVENT_WINDOW_FOCUS_GAINED && e.type != SDL_EVENT_WINDOW_FOCUS_LOST;
}

void Overlay_Toggle()
{
    if (!s_ready) return;
    s_open = !s_open;
    LOG_INFO("settings overlay {}", s_open ? "opened" : "closed");
    // text input (the texture pack folder field) only while the overlay is up
    if (s_window) s_open ? SDL_StartTextInput(s_window) : SDL_StopTextInput(s_window);
}

bool Overlay_IsOpen() { return s_open; }

bool Overlay_TakeHomePress() { return s_homePress.exchange(false); }

bool Overlay_ShotPending() { return s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt; }

void Overlay_Render(float w, float h)
{
    if (!s_ready) return;
    {
        std::vector<SDL_Event> msgs;
        std::deque<std::string> texts;
        {
            std::lock_guard lk(s_msgMutex);
            msgs.swap(s_msgs);
            texts.swap(s_texts);
        }
        for (auto& m : msgs)
        {
            if (m.type == SDL_EVENT_TEXT_INPUT)
            {
                m.text.text = texts.front().c_str();
                ImGui_ImplSDL3_ProcessEvent(&m);
                texts.pop_front();
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&m);
        }
    }
    if (s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt && !s_open) s_open = true;
    if (!s_open) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2(w, h);   // the bound target (an off-screen one for screenshots)
    // scale with the window: designed for 720p
    float scale = std::max(0.75f, ImGui::GetIO().DisplaySize.y / 720.0f);
    ImGui::GetStyle().FontScaleMain = scale;
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 size(std::min(vp->Size.x * 0.9f, 760.0f * scale), std::min(vp->Size.y * 0.85f, 560.0f * scale));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    bool open = true;
    if (ImGui::Begin("Settings", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoSavedSettings))
    {
        float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("panel", ImVec2(0, -bottom));
        SettingsUI_Draw(SettingsHost::InGame);
        ImGui::EndChild();
        if (ImGui::Button("Close")) open = false;
        ImGui::SameLine();
        if (ImGui::Button("Wii HOME Menu"))
        {
            s_homePress = true;   // the game opens it on the next controller read
            open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart game")) App_Restart();
        ImGui::SameLine();
        if (ImGui::Button("Quit game"))
        {
            LOG_INFO("quit from the settings overlay");
            Input_StopRumble();
            fflush(nullptr);
            _Exit(0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("F1 / Esc / both stick clicks close");
        SettingsUI_DrawVersion();
    }
    ImGui::End();
    if (!open) s_open = false;
    if (SettingsUI_TakeRestartRequest()) App_Restart();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt + 1000)
    {
        UI_SaveBoundTarget(s_dev, s_ctx, s_shotPath);
        LOG_INFO("settings overlay screenshot saved to {}", s_shotPath);
        s_shotOpenAt = 0;
        s_open = false;
    }
}
