// App: the Dear ImGui user interface. All user-facing text is New Zealand
// English (en-NZ): "colour", "centre", "licence" (noun), etc.
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <chrono>
#include <string>

#include "../camera/CameraWorker.h"
#include "../preview/Preview.h"
#include "Settings.h"

class App {
public:
    void Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Frame();
    void Shutdown();

    // True while something on screen is animating (preview, held buttons).
    bool NeedsContinuousRedraw() const { return previewActive_ || holding_; }

    Settings& GetSettings() { return settings_; }

    static void ApplyTheme(float dpiScale);

private:
    void ManagePreview(const CameraState& s);
    void DrawHeader(const CameraState& s);
    void DrawPreview(const CameraState& s, float width, float height);
    void DrawControlTab(const CameraState& s);
    void DrawImageTab(const CameraState& s);
    void DrawCameraTab(const CameraState& s);
    void DrawLogTab(const CameraState& s);
    void DrawDeveloperTab(const CameraState& s);
    void DrawAiTracking(const CameraState& s);
    void DrawGimbal(const CameraState& s);
    void DrawPresets(const CameraState& s);
    void UvcSlider(const CameraState& s, UvcCtl c, const char* label);
    std::string Diagnostics(const CameraState& s);

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;

    Settings settings_;
    CameraWorker worker_;
    Preview preview_;
    std::wstring previewAttemptPath_;  // stops a failed preview restarting every frame
    bool previewActive_ = false;
    bool holding_ = false;
    std::chrono::steady_clock::time_point lastStep_{};

    // Developer tab inputs.
    int devSelector_ = 6;
    char devHex_[256] = "";
    int devReceiver_ = 0;
    char devCmd_[16] = "";
    char devPayload_[256] = "";
};
