// Window (SDL3), D3D11 device and presentation. The window lives on its own thread, which
// also runs SDL's event loop (window, keyboard, mouse and gamepad events); the D3D11 swap
// chain presents into it from the render thread.
#include "video.h"
#include "../config.h"
#include "../log.h"
#include "../input/gamepad.h"
#include "../ui/overlay.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

VideoConfig g_videoConfig;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

static SDL_Window* s_window = nullptr;
static void* s_nativeWindow = nullptr;   // what the swap chain presents to (HWND on Windows)
static IDXGISwapChain1* s_swapchain = nullptr;
static ID3D11RenderTargetView* s_backbufferRTV = nullptr;
static std::atomic<bool> s_closed{ false };
static std::atomic<bool> s_resized{ false };
static std::atomic<bool> s_focused{ false };
static std::atomic<bool> s_minimized{ false };
static std::atomic<int> s_clientW{ 1 }, s_clientH{ 1 };
static ID3D11VertexShader* s_blitVS = nullptr;
static ID3D11PixelShader* s_blitPS = nullptr;
static ID3D11SamplerState* s_linearSampler = nullptr;
static ID3D11Buffer* s_blitCB = nullptr;
static ID3D11RasterizerState* s_blitRS = nullptr;
static ID3D11BlendState* s_blitBS = nullptr;
static ID3D11DepthStencilState* s_blitDS = nullptr;
// UI screenshots of a minimized test window (BKR_UI_SHOT_OVERLAY): drawn off screen
static ID3D11Texture2D* s_shotTex = nullptr;
static ID3D11RenderTargetView* s_shotRTV = nullptr;
// requests from other threads, handled on the window thread
static Uint32 s_evtFullscreen = 0, s_evtWindowSize = 0;

static void Quit()
{
    // Closing the window quits. Saves are written to the host NAND as the game issues
    // them, so there is nothing to flush; exit directly rather than running teardown while
    // guest/GX/audio threads are mid-operation.
    s_closed = true;
    LOG_INFO("window closed, exiting");
    Input_StopRumble();
    fflush(nullptr);
    _Exit(0);
}

// false while minimized (no client area): the swap chain keeps its size then
static bool UpdateClientSize()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    s_minimized = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED) || w <= 0 || h <= 0;
    if (s_minimized) return false;
    s_clientW = w;
    s_clientH = h;
    return true;
}

static void HandleEvent(const SDL_Event& e)
{
    if (Overlay_HandleEvent(e)) return;   // the settings overlay has the input
    Gamepad_HandleEvent(e);
    switch (e.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        Quit();
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        if (UpdateClientSize()) s_resized = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        s_focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        if (!s_focused) Input_StopRumble();
        if (g_config.pauseOnFocusLoss)
        {
            extern std::atomic<bool> g_pauseRequested;
            g_pauseRequested = !s_focused;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) break;
        if (e.key.scancode == SDL_SCANCODE_F11 || (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))
        {
            bool on = !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(s_window, on);
            g_config.fullscreen = on;
        }
        else if (e.key.scancode == SDL_SCANCODE_F1)
            Overlay_Toggle();
        break;
    default:
        if (e.type == s_evtFullscreen) SDL_SetWindowFullscreen(s_window, e.user.code != 0);
        else if (e.type == s_evtWindowSize && !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN))
            SDL_SetWindowSize(s_window, e.user.code >> 16, e.user.code & 0xFFFF);
        break;
    }
}

static void WindowThread(std::atomic<int>* ready)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // focus is checked by the input code
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        LOG_ERROR("SDL video init failed: {}", SDL_GetError());
        *ready = -1;
        return;
    }
    s_evtFullscreen = SDL_RegisterEvents(2);
    s_evtWindowSize = s_evtFullscreen + 1;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_videoConfig.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    s_window = SDL_CreateWindow("The Sims 2: Castaway - Recompiled", g_videoConfig.windowWidth, g_videoConfig.windowHeight, flags);
    if (!s_window)
    {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        *ready = -1;
        return;
    }
#ifdef _WIN32
    s_nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(s_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    s_nativeWindow = s_window;   // DXVK's native build presents to SDL windows
#endif
    UpdateClientSize();
    Gamepad_Init();
    *ready = 1;
    for (;;)
    {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100)) continue;
        do
        {
            HandleEvent(e);
        } while (SDL_PollEvent(&e));
        // the game draws its own Wii Remote pointer; the overlay needs the mouse cursor
        bool showCursor = Overlay_IsOpen();
        if (showCursor != SDL_CursorVisible()) showCursor ? SDL_ShowCursor() : SDL_HideCursor();
    }
}

static ID3DBlob* Compile(const char* src, const char* entry, const char* target)
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr))
    {
        LOG_ERROR("shader compile failed: {}", err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

static const char* kBlitHLSL = R"(
cbuffer C : register(b0) { float4 uvRect; float4 dstRect; };
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    V o;
    float2 t = float2((id << 1) & 2, id & 2) * 0.5;   // 0..1 quad corners (triangle strip of 4)
    t = float2(id & 1, id >> 1);
    o.pos = float4(lerp(dstRect.x, dstRect.z, t.x), lerp(dstRect.y, dstRect.w, t.y), 0, 1);
    o.uv = lerp(uvRect.xy, uvRect.zw, t);
    return o;
}
float4 ps(V i) : SV_Target { return float4(tex.Sample(samp, i.uv).rgb, 1.0); }
)";

static void CreateBackbufferRTV()
{
    ID3D11Texture2D* bb = nullptr;
    s_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    g_device->CreateRenderTargetView(bb, nullptr, &s_backbufferRTV);
    bb->Release();
}

// What a bug report needs to tell graphics problems apart: Wine/Proton (its D3D11 is DXVK
// or wined3d, its HLSL compiler vkd3d-shader) and which d3dcompiler DLL is loaded.
static void LogPlatform()
{
#ifdef _WIN32
    using WineVersionFn = const char*(__cdecl*)();
    if (auto wineVersion = (WineVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version"))
        LOG_INFO("running under Wine {}", wineVersion());
    if (HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll"))
    {
        wchar_t path[MAX_PATH];
        char utf8[MAX_PATH * 3];
        GetModuleFileNameW(dc, path, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
        LOG_INFO("HLSL compiler: {}", utf8);
    }
#endif
}

const char* Video_DeviceHelp()
{
#ifdef _WIN32
    return "Could not start Direct3D 11. Check that your graphics driver is installed and up to date.";
#else
    return "Could not start Direct3D 11 on Vulkan (DXVK). Check that a Vulkan driver for your GPU is "
           "installed (vulkaninfo lists it), e.g. mesa-vulkan-drivers or the NVIDIA driver.";
#endif
}

bool Video_Init()
{
    LogPlatform();
    std::atomic<int> ready{ 0 };
    std::thread(WindowThread, &ready).detach();
    while (ready == 0) std::this_thread::yield();
    if (ready < 0) return false;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &fl, 1, D3D11_SDK_VERSION, &g_device, nullptr, &g_ctx);
    if (FAILED(hr))
    {
        LOG_ERROR("D3D11CreateDevice failed {:08x}", (uint32_t)hr);
        return false;
    }
    IDXGIDevice* dxgiDev = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    IDXGIAdapter* adapter = nullptr;
    dxgiDev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
    DXGI_ADAPTER_DESC ad;
    adapter->GetDesc(&ad);
    char name[128] = {};
    for (int i = 0; i < 127 && ad.Description[i]; i++) name[i] = ad.Description[i] < 128 ? (char)ad.Description[i] : '?';
    LOG_INFO("D3D11 adapter: {} (feature level {:x})", name, (uint32_t)g_device->GetFeatureLevel());

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChainForHwnd(g_device, (HWND)s_nativeWindow, &sd, nullptr, nullptr, &s_swapchain);
    if (FAILED(hr))
    {
        LOG_ERROR("CreateSwapChainForHwnd failed {:08x}", (uint32_t)hr);
        return false;
    }
    factory->MakeWindowAssociation((HWND)s_nativeWindow, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
    adapter->Release();
    dxgiDev->Release();
    CreateBackbufferRTV();

    ID3DBlob* vs = Compile(kBlitHLSL, "vs", "vs_5_0");
    ID3DBlob* ps = Compile(kBlitHLSL, "ps", "ps_5_0");
    if (!vs || !ps)
    {
        LOG_ERROR("present shaders failed to compile");
        return false;
    }
    g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &s_blitVS);
    g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &s_blitPS);
    vs->Release();
    ps->Release();
    D3D11_SAMPLER_DESC smp{};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    g_device->CreateSamplerState(&smp, &s_linearSampler);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 32;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&bd, nullptr, &s_blitCB);
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    g_device->CreateRasterizerState(&rs, &s_blitRS);
    D3D11_BLEND_DESC bs{};
    bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&bs, &s_blitBS);
    D3D11_DEPTH_STENCIL_DESC ds{};
    g_device->CreateDepthStencilState(&ds, &s_blitDS);
    Overlay_Init(s_window, g_device, g_ctx);
    return true;
}

void Video_Present(ID3D11ShaderResourceView* srv, float u0, float v0, float u1, float v1)
{
    if (s_resized.exchange(false))
    {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (s_backbufferRTV) s_backbufferRTV->Release();
        s_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackbufferRTV();
    }
    float w = (float)s_clientW, h = (float)s_clientH;
    ID3D11RenderTargetView* target = s_backbufferRTV;
    if (Overlay_ShotPending() && (s_minimized || w < 64))
    {
        if (!s_shotTex)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1280; td.Height = 720; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            g_device->CreateTexture2D(&td, nullptr, &s_shotTex);
            g_device->CreateRenderTargetView(s_shotTex, nullptr, &s_shotRTV);
        }
        target = s_shotRTV;
        w = 1280;
        h = 720;
    }
    D3D11_VIEWPORT vp{ 0, 0, w, h, 0, 1 };
    g_ctx->RSSetViewports(1, &vp);
    float black[4] = { 0, 0, 0, 1 };
    g_ctx->OMSetRenderTargets(1, &target, nullptr);
    g_ctx->ClearRenderTargetView(target, black);
    if (srv)
    {
        // letterbox to target aspect
        float aspect = g_videoConfig.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
        float dw = w, dh = h;
        if (w / h > aspect) dw = h * aspect;
        else dh = w / aspect;
        float x0 = (w - dw) / 2, y0 = (h - dh) / 2;
        float cb[8] = { u0, v0, u1, v1,
                        x0 / w * 2 - 1, 1 - y0 / h * 2, (x0 + dw) / w * 2 - 1, 1 - (y0 + dh) / h * 2 };
        D3D11_MAPPED_SUBRESOURCE m;
        g_ctx->Map(s_blitCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, cb, sizeof(cb));
        g_ctx->Unmap(s_blitCB, 0);
        g_ctx->IASetInputLayout(nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_ctx->VSSetShader(s_blitVS, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &s_blitCB);
        g_ctx->PSSetShader(s_blitPS, nullptr, 0);
        g_ctx->PSSetShaderResources(0, 1, &srv);
        g_ctx->PSSetSamplers(0, 1, &s_linearSampler);
        g_ctx->RSSetState(s_blitRS);
        g_ctx->OMSetBlendState(s_blitBS, nullptr, 0xFFFFFFFF);
        g_ctx->OMSetDepthStencilState(s_blitDS, 0);
        g_ctx->GSSetShader(nullptr, nullptr, 0);
        g_ctx->Draw(4, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        g_ctx->PSSetShaderResources(0, 1, &nullSrv);
    }
    Overlay_Render(w, h);   // in-game settings, drawn over the game while it keeps running
    HRESULT hr = s_swapchain->Present(g_videoConfig.vsync ? 1 : 0, 0);
    if (FAILED(hr))
    {
        static int failures = 0;
        if (failures++ < 5)
            LOG_ERROR("Present failed {:08x} (device removed reason {:08x})", (uint32_t)hr, (uint32_t)g_device->GetDeviceRemovedReason());
    }
}

bool Video_WindowClosed() { return s_closed; }
bool Video_HasFocus() { return s_focused; }

static void PushUserEvent(Uint32 type, Sint32 code)
{
    if (!s_window || !type) return;
    SDL_Event e{};
    e.type = type;
    e.user.code = code;
    SDL_PushEvent(&e);
}
void Video_SetFullscreen(bool on) { PushUserEvent(s_evtFullscreen, on ? 1 : 0); }
void Video_SetWindowSize(int w, int h) { PushUserEvent(s_evtWindowSize, (Sint32)((std::min(w, 0x7FFF) << 16) | std::min(h, 0xFFFF))); }
void* Video_Window() { return s_window; }
