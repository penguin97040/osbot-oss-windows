// App: the Dear ImGui user interface. All user-facing text is New Zealand
// English (en-NZ): "colour", "centre", "licence" (noun), etc.
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <chrono>
#include <string>
#include <vector>

#include "../camera/CameraWorker.h"
#include "../preview/Preview.h"
#include "CameraUse.h"
#include "CompactPolicy.h"
#include "Settings.h"

struct ImFont;

class App {
public:
    void Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Frame();
    void Shutdown();

    // True while something on screen is animating (preview, held buttons).
    bool NeedsContinuousRedraw() const { return previewActive_ || holding_; }
    // Called instead of Frame() while the window is minimised or hidden, so the
    // preview still pauses and the camera-use check still runs.
    void UpdateHidden();

    Settings& GetSettings() { return settings_; }

    // Compact view hides the preview and narrows the window to the controls.
    // Call once after creating the (still hidden) window: starts compact if another
    // app is using the camera.
    void DecideStartupView(HWND hwnd);
    bool Compact() const { return compact_; }
    // Width (96-DPI units) to restore in full view; 0 if not known yet.
    int FullWindowWidth() const { return fullWidth96_; }
    // Outer window width in pixels for the compact view at this DPI.
    static int CompactWindowWidth(UINT dpi);

    static void ApplyTheme(float dpiScale);
    // The window background (0xRRGGBB), shared with the title bar and swap-chain clear in main.cpp.
    static constexpr unsigned kBackgroundRgb = 0x06090D;
    // The bold face for headings; null falls back to the default font.
    static void SetHeadingFont(ImFont* font);

private:
    void Update(const CameraState& s);
    void CheckCameraUse();
    void SetCompact(bool on);
    void ApplyWindowSize();
    void ManagePreview(const CameraState& s);
    void DrawHeader(const CameraState& s);
    void RefreshFormats();
    void DrawPreviewToolbar(const CameraState& s);
    void DrawPreview(const CameraState& s, float width, float height);
    void DrawControlTab(const CameraState& s);
    void DrawImageTab(const CameraState& s);
    void DrawCameraTab(const CameraState& s);
    void DrawLogTab(const CameraState& s);
    void DrawDeveloperTab(const CameraState& s);
    void DrawAiTracking(const CameraState& s);
    void DrawGimbal(const CameraState& s);
    void DrawPresets(const CameraState& s);
    void UvcSlider(const CameraState& s, UvcCtl c, const char* label, float labelWidth, bool autoColumn,
                   float unitSlot = 0.0f);
    std::string Diagnostics(const CameraState& s);

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;

    Settings settings_;
    CameraWorker worker_;
    Preview preview_;
    std::wstring previewAttemptPath_;  // stops a failed preview restarting every frame
    std::string previewFormatKey_;     // chosen preview format; empty = default. Not saved,
    std::wstring previewFormatDevice_; // so the app always opens at the default format.
    // The preview's formats, refreshed only when Preview::FormatGeneration() or
    // previewFormatKey_ changes. def/cur point into formats (null if not found).
    struct FormatCache {
        uint64_t generation = UINT64_MAX;
        std::string requested;
        std::vector<PreviewFormat> formats;
        std::string defaultKey;
        const PreviewFormat* def = nullptr;
        const PreviewFormat* cur = nullptr;
    } formats_;
    bool compact_ = false;
    bool paused_ = false;  // preview paused because another window is active
    bool resizePending_ = false;  // applied by the next frame, once the window isn't minimised
    int fullWidth96_ = 0;
    compact::Policy compactPolicy_;
    CameraUse cameraUse_;
    std::chrono::steady_clock::time_point lastUseCheck_{};
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
