// Preview: reads frames from the camera with Media Foundation on a background
// thread and uploads the newest one to a D3D11 texture for ImGui to draw.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../Safety.h"
#include "PreviewFormat.h"

struct PreviewReadState;

class Preview {
public:
    ~Preview();

    // formatKey is a PreviewFormat::Key(); empty (or unknown) picks the default format.
    void Start(const std::wstring& devicePath, const std::string& formatKey = {});
    void Stop();
    bool Running() const { return running_; }
    const std::wstring& DevicePath() const { return path_; }
    const std::string& RequestedFormat() const { return requestedFormat_; }

    // UI thread: uploads the newest frame (if any) and returns the texture view.
    ID3D11ShaderResourceView* Update(ID3D11Device* device, ID3D11DeviceContext* ctx, int* width, int* height);

    std::string Error();   // non-empty when the preview stopped because of a problem
    std::string Format();  // e.g. "1280×720 MJPG 30 fps"

    // The camera's native formats (in the order offered) and the default choice. Kept
    // from the last time the camera was opened, so they survive a restart.
    std::vector<PreviewFormat> Formats();
    std::string DefaultFormatKey();
    std::string CurrentFormatKey();
    // Changes whenever Formats(), DefaultFormatKey() or CurrentFormatKey() may have,
    // so the UI can keep its own copy instead of fetching them every frame.
    uint64_t FormatGeneration();

    // A blurred copy of the last frame, shown while the preview is paused. MakeStill
    // keeps the previous one if no frame has arrived since. width/height give the
    // frame's shape, not the texture's (tiny) size. Null when there's none.
    void MakeStill(ID3D11Device* device);
    void ClearStill();
    ID3D11ShaderResourceView* Still(int* width, int* height) const;

private:
    void Run(std::wstring path, std::string formatKey, std::shared_ptr<PreviewReadState> state);
    void SetError(const std::string& e);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::wstring path_;
    std::string requestedFormat_;
    std::shared_ptr<PreviewReadState> readState_;

    std::mutex frameMutex_;
    std::vector<uint8_t> frame_;  // BGRA, tightly packed
    int frameW_ = 0, frameH_ = 0;
    uint64_t frameId_ = 0;
    std::string error_;
    std::string format_;
    std::string formatKey_;
    std::vector<PreviewFormat> formats_;
    std::string defaultFormatKey_;
    uint64_t formatGen_ = 0;

    safety::UploadProgress upload_;
    int texW_ = 0, texH_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> stillSrv_;  // UI thread only
    int stillW_ = 0, stillH_ = 0;
};
