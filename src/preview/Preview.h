// Preview: reads frames from the camera with Media Foundation on a background
// thread and uploads the newest one to a D3D11 texture for ImGui to draw.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../Safety.h"

struct PreviewReadState;

// One of the camera's native capture formats.
struct PreviewFormat {
    uint32_t width = 0, height = 0, fpsNum = 0, fpsDen = 1;
    std::string fourcc;  // e.g. "NV12", "MJPG"

    double Fps() const { return fpsDen ? static_cast<double>(fpsNum) / fpsDen : 0.0; }
    // Exact identity, used to ask for this format again.
    std::string Key() const {
        char buf[64];
        snprintf(buf, sizeof buf, "%ux%u:%s:%u/%u", width, height, fourcc.c_str(), fpsNum, fpsDen);
        return buf;
    }
    // e.g. "1280×720 NV12 30 fps"
    std::string Label() const {
        char buf[64];
        snprintf(buf, sizeof buf, "%u\xC3\x97%u %s %.0f fps", width, height, fourcc.c_str(), Fps());
        return buf;
    }
};

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

    // The camera's native formats (largest first) and the default choice. Kept
    // from the last time the camera was opened, so they survive a restart.
    std::vector<PreviewFormat> Formats();
    std::string DefaultFormatKey();
    std::string CurrentFormatKey();

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

    safety::UploadProgress upload_;
    int texW_ = 0, texH_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
};
