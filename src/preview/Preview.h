// Preview: reads frames from the camera with Media Foundation on a background
// thread and uploads the newest one to a D3D11 texture for ImGui to draw.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Preview {
public:
    ~Preview();

    void Start(const std::wstring& devicePath);
    void Stop();
    bool Running() const { return running_; }
    const std::wstring& DevicePath() const { return path_; }

    // UI thread: uploads the newest frame (if any) and returns the texture view.
    ID3D11ShaderResourceView* Update(ID3D11Device* device, ID3D11DeviceContext* ctx, int* width, int* height);

    std::string Error();   // non-empty when the preview stopped because of a problem
    std::string Format();  // e.g. "1280×720 MJPG 30 fps"

private:
    void Run(std::wstring path);
    void SetError(const std::string& e);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::wstring path_;

    std::mutex frameMutex_;
    std::vector<uint8_t> frame_;  // BGRA, tightly packed
    int frameW_ = 0, frameH_ = 0;
    uint64_t frameId_ = 0;
    std::string error_;
    std::string format_;

    uint64_t uploadedId_ = 0;
    int texW_ = 0, texH_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
};
