// osbot-oss-windows: entry point. Creates the window, the D3D11 device and the
// Dear ImGui context, then runs the message/render loop. Based on the Dear ImGui
// example_win32_directx11 (MIT).
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <mfapi.h>
#include <objbase.h>

#include <algorithm>

#include "app/App.h"
#include "app/Log.h"
#include "app/Settings.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

const wchar_t* kWindowClass = L"OsbotOssWindowsMain";

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_occluded = false;
UINT g_resizeW = 0, g_resizeH = 0;
float g_pendingDpiScale = 0.0f;

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // no GPU: fall back to the WARP software rasteriser
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// Dark title bar on Windows 10 (build 18985+) and Windows 11.
void UseDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark)))
        DwmSetWindowAttribute(hwnd, 19 /* pre-20H1 value */, &dark, sizeof dark);
    COLORREF caption = RGB(0x0F, 0x11, 0x15);  // matches the window background (Windows 11 only)
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    // Segoe UI ships with Windows; we load it from the system rather than
    // redistributing it. Falls back to Dear ImGui's built-in font.
    wchar_t winDir[MAX_PATH];
    UINT n = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring wpath = std::wstring(winDir) + L"\\Fonts\\segoeui.ttf";
        if (GetFileAttributesW(wpath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            char path[MAX_PATH * 3];
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, path, sizeof path, nullptr, nullptr);
            if (io.Fonts->AddFontFromFileTTF(path)) return;
        }
    }
    io.Fonts->AddFontDefaultVector();
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_resizeW = LOWORD(lParam);
        g_resizeH = HIWORD(lParam);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        mmi->ptMinTrackSize.x = static_cast<LONG>(820 * scale);
        mmi->ptMinTrackSize.y = static_cast<LONG>(600 * scale);
        return 0;
    }
    case WM_DPICHANGED: {
        g_pendingDpiScale = HIWORD(wParam) / 96.0f;
        const RECT* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no Alt menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    // One instance only: two copies would fight over the camera.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\osbot-oss-windows-single-instance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    logx::OpenFile(Settings::Directory() + L"\\osbot-oss.log");
    LOG_INFO("osbot-oss-windows %s starting.", OSBOT_VERSION_STRING);

    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    App app;
    app.GetSettings().Load();
    Settings& settings = app.GetSettings();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0F, 0x11, 0x15));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const int w = std::clamp(settings.windowW, 820, 7680);
    const int h = std::clamp(settings.windowH, 600, 4320);
    HWND hwnd = CreateWindowW(kWindowClass, L"osbot-oss — OBSBOT camera control", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(w * scale), static_cast<int>(h * scale),
                              nullptr, nullptr, instance, nullptr);
    UseDarkTitleBar(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        MessageBoxW(hwnd, L"Couldn't start Direct3D 11. Please update your graphics driver.", L"osbot-oss",
                    MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // we keep our own settings file
    App::ApplyTheme(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    LoadFonts();

    app.Init(hwnd, g_device, g_context);

    const float clear[4] = {0.059f, 0.067f, 0.082f, 1.0f};
    bool done = false;
    int busyFrames = 0;
    while (!done) {
        // Idle politely: wait for input (or 250 ms for status updates) unless
        // the preview or a held button needs every frame. After any input we
        // keep drawing a few frames, because Dear ImGui spreads a fast click
        // (button down + up in one batch) over several frames.
        if (!app.NeedsContinuousRedraw() && busyFrames == 0)
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);

        MSG msg;
        bool hadInput = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
            if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
                (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) || msg.message == WM_MOUSELEAVE)
                hadInput = true;
        }
        if (done) break;
        if (hadInput)
            busyFrames = 6;
        else if (busyFrames > 0)
            --busyFrames;

        if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(50);
            continue;
        }
        g_occluded = false;

        if (g_resizeW && g_resizeH) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }
        if (g_pendingDpiScale > 0) {
            App::ApplyTheme(g_pendingDpiScale);
            g_pendingDpiScale = 0;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        ImGui::Render();
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_occluded = g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    }

    // Remember the window size in 96-DPI units.
    WINDOWPLACEMENT wp{sizeof wp};
    if (GetWindowPlacement(hwnd, &wp)) {
        const float s = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        settings.windowW = static_cast<int>((wp.rcNormalPosition.right - wp.rcNormalPosition.left) / s);
        settings.windowH = static_cast<int>((wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) / s);
    }
    app.Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClass, instance);
    MFShutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
