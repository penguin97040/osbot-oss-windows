#include "Preview.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cmath>
#include <cstring>
#include <cwctype>

#include "../app/Log.h"

using Microsoft::WRL::ComPtr;

namespace {

// DirectShow and Media Foundation expose the same camera under different
// interface-class GUIDs, so compare the path up to the trailing "#{guid}".
std::wstring InstanceKey(const std::wstring& path) {
    std::wstring s = path;
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    size_t pos = s.rfind(L"#{");
    if (pos != std::wstring::npos) s.resize(pos);
    return s;
}

std::string FourCc(const GUID& subtype) {
    if (subtype == MFVideoFormat_MJPG) return "MJPG";
    if (subtype == MFVideoFormat_NV12) return "NV12";
    if (subtype == MFVideoFormat_YUY2) return "YUY2";
    if (subtype == MFVideoFormat_H264) return "H264";
    if (subtype == MFVideoFormat_RGB32) return "RGB32";
    char buf[5] = {};
    std::memcpy(buf, &subtype.Data1, 4);
    for (char& c : buf)
        if (c && (c < 32 || c > 126)) c = '?';
    return buf;
}

std::string FriendlyError(HRESULT hr) {
    if (hr == E_ACCESSDENIED)
        return "Windows blocked camera access. Open Settings > Privacy & security > Camera and turn on "
               "\"Let desktop apps access your camera\".";
#ifdef MF_E_HW_MFT_FAILED_START_STREAMING
    if (hr == MF_E_HW_MFT_FAILED_START_STREAMING)
        return "The camera could not start. Another app (Teams, Zoom, OBS, the Camera app...) may be using it.";
#endif
#ifdef MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED
    if (hr == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED)
        return "The camera was disconnected or taken over by another app.";
#endif
#ifdef MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED
    if (hr == MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED)
        return "Another app took over the camera.";
#endif
    return "Preview failed: " + logx::HrText(hr);
}

// Scores a native format: prefer ~1280x720 at 30 fps or more.
double ScoreType(IMFMediaType* type) {
    UINT32 w = 0, h = 0, num = 0, den = 1;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
    const double fps = den ? static_cast<double>(num) / den : 0;
    double score = -std::fabs(static_cast<double>(w) * h - 1280.0 * 720.0) / 1e5;
    if (fps < 29) score -= 100;
    if (fps > 31) score -= 1;  // 30 fps is plenty for a preview
    GUID sub{};
    type->GetGUID(MF_MT_SUBTYPE, &sub);
    if (sub == MFVideoFormat_H264) score -= 1000;
    return score;
}

}  // namespace

Preview::~Preview() { Stop(); }

void Preview::Start(const std::wstring& devicePath) {
    Stop();
    path_ = devicePath;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        error_.clear();
        format_.clear();
        frame_.clear();
        frameId_ = 0;
        uploadedId_ = 0;
    }
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this, devicePath] { Run(devicePath); });
}

void Preview::Stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

std::string Preview::Error() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return error_;
}

std::string Preview::Format() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return format_;
}

void Preview::SetError(const std::string& e) {
    LOG_WARN("%s", e.c_str());
    std::lock_guard<std::mutex> lock(frameMutex_);
    error_ = e;
}

void Preview::Run(std::wstring path) {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::wstring key = InstanceKey(path);

    ComPtr<IMFMediaSource> source;
    {
        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(attrs.GetAddressOf(), 1);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFEnumDeviceSources(attrs.Get(), &devices, &count);
        if (SUCCEEDED(hr)) {
            for (UINT32 i = 0; i < count; ++i) {
                WCHAR* link = nullptr;
                UINT32 len = 0;
                if (!source && SUCCEEDED(devices[i]->GetAllocatedString(
                                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &len))) {
                    if (InstanceKey(link) == key) {
                        hr = devices[i]->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));
                        if (FAILED(hr)) SetError(FriendlyError(hr));
                    }
                    CoTaskMemFree(link);
                }
                devices[i]->Release();
            }
            CoTaskMemFree(devices);
        }
        if (!source) {
            if (Error().empty()) SetError("Preview: the camera was not found by Media Foundation.");
            running_ = false;
            if (SUCCEEDED(hrCom)) CoUninitialize();
            return;
        }
    }

    ComPtr<IMFSourceReader> reader;
    {
        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(attrs.GetAddressOf(), 1);
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        HRESULT hr = MFCreateSourceReaderFromMediaSource(source.Get(), attrs.Get(), reader.GetAddressOf());
        if (FAILED(hr)) {
            SetError(FriendlyError(hr));
            source->Shutdown();
            running_ = false;
            if (SUCCEEDED(hrCom)) CoUninitialize();
            return;
        }
    }

    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    // Pick the best native format, then ask the reader to convert to RGB32.
    {
        ComPtr<IMFMediaType> best;
        double bestScore = -1e18;
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(reader->GetNativeMediaType(stream, i, t.GetAddressOf()))) break;
            double s = ScoreType(t.Get());
            if (s > bestScore) {
                bestScore = s;
                best = t;
            }
        }
        if (best) reader->SetCurrentMediaType(stream, nullptr, best.Get());

        UINT32 w = 0, h = 0, num = 0, den = 1;
        GUID sub{};
        if (best) {
            MFGetAttributeSize(best.Get(), MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(best.Get(), MF_MT_FRAME_RATE, &num, &den);
            best->GetGUID(MF_MT_SUBTYPE, &sub);
        }
        ComPtr<IMFMediaType> rgb;
        MFCreateMediaType(rgb.GetAddressOf());
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        HRESULT hr = reader->SetCurrentMediaType(stream, nullptr, rgb.Get());
        if (FAILED(hr)) {
            SetError(FriendlyError(hr));
            source->Shutdown();
            running_ = false;
            if (SUCCEEDED(hrCom)) CoUninitialize();
            return;
        }
        char fmt[96];
        snprintf(fmt, sizeof fmt, "%u\xC3\x97%u %s %.0f fps", w, h, FourCc(sub).c_str(),
                 den ? static_cast<double>(num) / den : 0.0);
        std::lock_guard<std::mutex> lock(frameMutex_);
        format_ = fmt;
    }

    UINT32 width = 0, height = 0;
    LONG defaultStride = 0;
    {
        ComPtr<IMFMediaType> current;
        reader->GetCurrentMediaType(stream, current.GetAddressOf());
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
        UINT32 strideAttr = 0;
        if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideAttr)))
            defaultStride = static_cast<LONG>(strideAttr);
        else
            defaultStride = static_cast<LONG>(width * 4);
    }
    LOG_INFO("Preview started (%s).", Format().c_str());

    std::vector<uint8_t> scratch;
    while (!stopRequested_) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = reader->ReadSample(stream, 0, nullptr, &flags, nullptr, sample.GetAddressOf());
        if (FAILED(hr)) {
            SetError(FriendlyError(hr));
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            SetError("The camera stopped sending video.");
            break;
        }
        if (!sample || width == 0 || height == 0) continue;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) continue;

        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        ComPtr<IMF2DBuffer> buffer2d;
        bool locked2d = false;
        BYTE* raw = nullptr;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
            locked2d = true;
        } else {
            DWORD maxLen = 0, curLen = 0;
            if (FAILED(buffer->Lock(&raw, &maxLen, &curLen))) continue;
            pitch = defaultStride;
            scan0 = pitch < 0 ? raw + static_cast<size_t>(-pitch) * (height - 1) : raw;
        }

        scratch.resize(static_cast<size_t>(width) * height * 4);
        for (UINT32 y = 0; y < height; ++y) {
            const uint8_t* src = scan0 + static_cast<ptrdiff_t>(pitch) * y;
            uint8_t* dst = scratch.data() + static_cast<size_t>(y) * width * 4;
            std::memcpy(dst, src, static_cast<size_t>(width) * 4);
            for (UINT32 x = 0; x < width; ++x) dst[x * 4 + 3] = 0xFF;  // RGB32 alpha is undefined
        }
        if (locked2d)
            buffer2d->Unlock2D();
        else
            buffer->Unlock();

        std::lock_guard<std::mutex> lock(frameMutex_);
        frame_.swap(scratch);
        frameW_ = static_cast<int>(width);
        frameH_ = static_cast<int>(height);
        ++frameId_;
    }

    reader.Reset();
    source->Shutdown();
    LOG_INFO("Preview stopped.");
    running_ = false;
    if (SUCCEEDED(hrCom)) CoUninitialize();
}

ID3D11ShaderResourceView* Preview::Update(ID3D11Device* device, ID3D11DeviceContext* ctx, int* width, int* height) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (frameId_ == 0) return nullptr;
    if (frameId_ != uploadedId_) {
        if (!texture_ || texW_ != frameW_ || texH_ != frameH_) {
            srv_.Reset();
            texture_.Reset();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(frameW_);
            desc.Height = static_cast<UINT>(frameH_);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()))) return nullptr;
            if (FAILED(device->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf()))) return nullptr;
            texW_ = frameW_;
            texH_ = frameH_;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const size_t rowBytes = static_cast<size_t>(frameW_) * 4;
            for (int y = 0; y < frameH_; ++y)
                std::memcpy(static_cast<uint8_t*>(mapped.pData) + mapped.RowPitch * y,
                            frame_.data() + rowBytes * y, rowBytes);
            ctx->Unmap(texture_.Get(), 0);
        }
        uploadedId_ = frameId_;
    }
    *width = texW_;
    *height = texH_;
    return srv_.Get();
}
