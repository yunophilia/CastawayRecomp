// Launcher shown on every start: game data status (install / repair from the disc image,
// full file check), settings, and Play. Its own small window and D3D11 device; both are
// gone before the game creates its window.
#include "settings_ui.h"
#include "ui_shot.h"
#include "../config.h"
#include "../install/verify.h"
#include "../gx/video.h"
#include "../log.h"
#include "../region.h"
#include "../install_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>

#include <atomic>
#include <d3d11.h>
#include <dxgi.h>
#include <cstring>
#include <filesystem>
#include <thread>
#ifdef _WIN32
#ifdef _WIN32
#include <windows.h>
#endif
#endif

namespace fs = std::filesystem;


namespace
{
ID3D11Device* s_dev = nullptr;
ID3D11DeviceContext* s_ctx = nullptr;
IDXGISwapChain* s_swap = nullptr;
ID3D11RenderTargetView* s_rtv = nullptr;
bool s_quit = false;
bool s_resize = false;
SDL_Window* s_window = nullptr;
void CreateRTV();

// One launcher frame showing install progress; false when the player cancels or closes.
bool DrawProgressFrame(const char* line, double fraction)
{
    SDL_Event e;
    bool cancel = false;
    while (SDL_PollEvent(&e))
    {
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) cancel = s_quit = true;
        else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) s_resize = true;
    }
    if (s_resize)
    {
        s_resize = false;
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(s_window, &pw, &ph);
        if (pw > 0 && ph > 0 && !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED))   // keep the size while minimized
        {
            if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
            s_swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("install", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Dummy(ImVec2(0, vp->Size.y * 0.35f));
    ImGui::TextWrapped("%s", line);
    ImGui::ProgressBar((float)fraction, ImVec2(-1, 0));
    if (ImGui::Button("Cancel")) cancel = true;
    ImGui::End();
    ImGui::Render();
    const float clear[4] = { 0.06f, 0.06f, 0.05f, 1 };
    s_ctx->OMSetRenderTargets(1, &s_rtv, nullptr);
    s_ctx->ClearRenderTargetView(s_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    s_swap->Present(1, 0);
    return !cancel;
}

void CreateRTV()
{
    ID3D11Texture2D* bb = nullptr;
    s_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    s_dev->CreateRenderTargetView(bb, nullptr, &s_rtv);
    bb->Release();
}

struct State
{
    fs::path data;
    DataStatus status = DataStatus::Missing;
    std::string problem;
    std::string release;            // "USA (R42E69)" once installed
    std::string releaseError;       // this build cannot play the installed release
    bool promptInstall = false;     // ask for the disc image after the window is shown
    std::thread verifyThread;
    VerifyProgress verify;
    std::atomic<bool> verifying{ false };
    std::string verifyResult;
};

void Check(State& st)
{
    st.status = Install_QuickCheck(st.data, st.problem);
    st.release.clear();
    st.releaseError.clear();
    if (st.status == DataStatus::Ok)
    {
        std::string id = Release_InstalledId(st.data);
        if (const GameRelease* r = Release_Find(id)) st.release = std::string(r->name) + " (" + id + ")";
        Region_Select(id, st.releaseError);
    }
    LOG_INFO("launcher: game data {} {}", st.data.string(),
             st.status == DataStatus::Ok ? "ok" : st.status == DataStatus::Missing ? "not installed" : "damaged: " + st.problem);
}

void StartVerify(State& st)
{
    if (st.verifying) return;
    if (st.verifyThread.joinable()) st.verifyThread.join();
    st.verify.cancel = false;
    st.verifying = true;
    st.verifyResult.clear();
    st.verifyThread = std::thread([&st] {
        std::string problem;
        DataStatus r = Install_FullCheck(st.data, st.verify, problem);
        if (st.verify.cancel) st.verifyResult = "Check canceled.";
        else if (r == DataStatus::Ok) st.verifyResult = "All game files are intact.";
        else
        {
            st.verifyResult = "Damaged: " + problem;
            st.status = r;
            st.problem = problem;
        }
        st.verifying = false;
    });
}

void DrawHome(State& st, bool& play, const std::string& isoPath, const fs::path& installDest)
{
    ImGui::Spacing();
    const ImVec4 green(0.45f, 0.80f, 0.40f, 1), red(0.90f, 0.40f, 0.30f, 1);
    switch (st.status)
    {
    case DataStatus::Ok: ImGui::TextColored(green, "Game data installed: %s", st.release.c_str()); break;
    case DataStatus::Missing: ImGui::TextColored(red, "Game data not installed"); break;
    case DataStatus::Corrupt: ImGui::TextColored(red, "Game data damaged: %s", st.problem.c_str()); break;
    }
    if (!st.releaseError.empty()) ImGui::TextColored(red, "%s", st.releaseError.c_str());
    ImGui::TextDisabled("%s", st.data.string().c_str());
    ImGui::Spacing();

    bool ready = st.status == DataStatus::Ok && st.releaseError.empty() && !st.verifying;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("  Play  ", ImVec2(ImGui::GetFontSize() * 10, ImGui::GetFontSize() * 2.4f))) play = true;
    ImGui::EndDisabled();
    if (ready && !ImGui::IsAnyItemActive() && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);

    ImGui::Spacing();
    ImGui::SeparatorText("Game data");
    ImGui::BeginDisabled(st.verifying);
    if (ImGui::Button(st.status == DataStatus::Ok ? "Reinstall from disc image..." : "Install from disc image..."))
    {
        fs::path dest = st.status == DataStatus::Missing ? installDest : st.data;
        if (InstallUI_Run(s_window, dest, isoPath, st.status == DataStatus::Corrupt ? st.problem : "", DrawProgressFrame))
            st.data = dest;
        Check(st);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(st.status == DataStatus::Missing);
    if (ImGui::Button("Check all game files")) StartVerify(st);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (st.verifying)
    {
        uint64_t total = st.verify.total ? st.verify.total.load() : 1;
        ImGui::ProgressBar((float)((double)st.verify.done / total), ImVec2(ImGui::GetFontSize() * 20, 0), "Checking...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) st.verify.cancel = true;
    }
    else if (!st.verifyResult.empty())
        ImGui::TextWrapped("%s", st.verifyResult.c_str());
    ImGui::TextDisabled("Install reads your own disc image (ISO, RVZ, WIA, WBFS); it is not needed afterwards.");

    ImGui::Spacing();
    ImGui::SeparatorText("Controls");
    ImGui::TextWrapped("Gamepad: left stick moves the Sim, right stick moves the pointer, A / B are A / B, "
                       "LB / RB are the Nunchuk Z / C, X / Y are 1 / 2, Back / Start are - / +, D-pad is the D-pad.");
    ImGui::TextWrapped("Keyboard and mouse: WASD moves, the mouse points (left click A, right click B), Space A, "
                       "Backspace B, Z / C Nunchuk Z / C, 1 / 2, Enter +, Tab -, arrow keys D-pad, IJKL move the pointer.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Where the game asks to tilt the Nunchuk, use the D-pad. An unused pointer leaves the screen "
                        "after a few seconds, like pointing the Wii Remote away.");
    ImGui::TextDisabled("In game: F1 or both stick clicks open the settings. Guide or Back + Start is the Wii HOME button.");
    ImGui::PopTextWrapPos();
}
}   // namespace

// Returns true to start the game, with dataDir set to the installed data.
bool Launcher_Run(std::string& dataDir, const std::string& isoPath, const fs::path& installDest)
{
#ifdef _WIN32
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // the disc image file dialog
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        LOG_WARN("launcher: SDL video unavailable ({}), starting the game directly", SDL_GetError());
        return true;
    }
    SDL_Window* window = s_window = SDL_CreateWindow("The Sims 2: Castaway - Recompiled", 900, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
#ifdef _WIN32
    void* native = window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr) : nullptr;
#else
    void* native = window;   // DXVK's native build presents to SDL windows
#endif
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = (HWND)native;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (native && FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                                       &sd, &s_swap, &s_dev, nullptr, &s_ctx)))
    {
        // the game could not start either (and DXVK does not survive a second attempt)
        SDL_DestroyWindow(window);
        FATAL("{}", Video_DeviceHelp());
    }
    if (!native)
    {
        LOG_WARN("launcher: no window or D3D11 device, starting the game directly");
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        return true;
    }
    CreateRTV();
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGuiContext* imgui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    SettingsUI_LoadFont();
    SettingsUI_ApplyStyle();
    ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX11_Init(s_dev, s_ctx);
    SDL_StartTextInput(window);

    State st;
    st.data = dataDir.empty() ? installDest : fs::path(dataDir);
    Check(st);
    // test aid: BKR_UI_SHOT=<file.bmp> saves the launcher (BKR_UI_SHOT_TAB=settings: the
    // settings tab) and quits, without asking for a disc image
    const char* shot = getenv("BKR_UI_SHOT");
    const char* shotTab = getenv("BKR_UI_SHOT_TAB");
    st.promptInstall = st.status != DataStatus::Ok && !shot;

    bool play = false;
    s_quit = false;
    int frames = 0;
    while (!s_quit && !play)
    {
        SDL_Event e;
        bool any = false;
        while (SDL_PollEvent(&e))
        {
            any = true;
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) s_quit = true;
            else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) s_resize = true;
        }
        if (s_quit) break;
        if (!any && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) && !shot)
        {
            SDL_WaitEventTimeout(nullptr, 50);
            continue;
        }
        if (s_resize)
        {
            s_resize = false;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(s_window, &pw, &ph);
            if (pw > 0 && ph > 0 && !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED))   // keep the size while minimized
            {
                if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
                s_swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
                CreateRTV();
            }
        }
        // first start, deleted or damaged data: ask for the disc image once the window is up
        if (st.promptInstall && frames++ > 2)
        {
            st.promptInstall = false;
            fs::path dest = st.status == DataStatus::Missing ? installDest : st.data;
            if (InstallUI_Run(s_window, dest, isoPath, st.status == DataStatus::Corrupt ? st.problem : "", DrawProgressFrame))
                st.data = dest;
            Check(st);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (shot)
        {
            // screenshots run minimized (no client area): lay out for the back buffer
            DXGI_SWAP_CHAIN_DESC d;
            s_swap->GetDesc(&d);
            io.DisplaySize = ImVec2((float)d.BufferDesc.Width, (float)d.BufferDesc.Height);
        }
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "The Sims 2: Castaway");
        ImGui::PopFont();
        ImGui::TextDisabled("Recompiled for PC");
        float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("tabs", ImVec2(0, -bottom));
        if (ImGui::BeginTabBar("launcherTabs"))
        {
            if (ImGui::BeginTabItem("Play"))
            {
                DrawHome(st, play, isoPath, installDest);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings", nullptr,
                                    frames < 2 && shotTab && !strcmp(shotTab, "settings") ? ImGuiTabItemFlags_SetSelected : 0))
            {
                SettingsUI_Draw(SettingsHost::Launcher);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        if (ImGui::Button("Quit")) s_quit = true;
        SettingsUI_DrawVersion();
        ImGui::End();

        ImGui::Render();
        const float clear[4] = { 0.06f, 0.06f, 0.05f, 1 };
        s_ctx->OMSetRenderTargets(1, &s_rtv, nullptr);
        s_ctx->ClearRenderTargetView(s_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (shot && ++frames > 5)
        {
            UI_SaveBoundTarget(s_dev, s_ctx, shot);
            s_quit = true;
        }
        s_swap->Present(1, 0);
    }

    st.verify.cancel = true;
    if (st.verifyThread.joinable()) st.verifyThread.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imgui);
    if (s_rtv) s_rtv->Release();
    s_swap->Release();
    s_ctx->Release();
    s_dev->Release();
    s_rtv = nullptr;
    SDL_DestroyWindow(window);
    // the game window starts SDL video again on its own thread
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
#ifdef _WIN32
    if (SUCCEEDED(co)) CoUninitialize();
#endif
    if (play) dataDir = st.data.string();
    LOG_INFO("launcher: {}", play ? "play" : "quit");
    return play;
}
