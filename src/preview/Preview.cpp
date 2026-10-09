#include "Preview.h"
#include "AsyncMailbox.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <exception>

#include "../app/Log.h"

using Microsoft::WRL::ComPtr;

struct PreviewReadResult {
    HRESULT status = S_OK;
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
};
struct PreviewReadState : AsyncMailbox<PreviewReadResult> {};

namespace {

class ReaderCallback final : public IMFSourceReaderCallback {
public:
    explicit ReaderCallback(std::shared_ptr<PreviewReadState> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IMFSourceReaderCallback)) return E_NOINTERFACE;
        *out = static_cast<IMFSourceReaderCallback*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        state_->Deliver({hr, flags, sample});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent* event) override {
        HRESULT status = S_OK;
        if (event && SUCCEEDED(event->GetStatus(&status)) && FAILED(status)) state_->Deliver({status, 0, {}});
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<PreviewReadState> state_;
};

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
        upload_.uploaded = 0;
        texture_.Reset();
        srv_.Reset();
        texW_ = texH_ = 0;
    }
    stopRequested_ = false;
    running_ = true;
    readState_ = std::make_shared<PreviewReadState>();
    thread_ = std::thread([this, devicePath, state = readState_] { Run(devicePath, state); });
}

void Preview::Stop() {
    stopRequested_ = true;
    if (readState_) readState_->Cancel();
    if (thread_.joinable()) thread_.join();
    running_ = false;
    readState_.reset();
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

void Preview::Run(std::wstring path, std::shared_ptr<PreviewReadState> state) {
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCom)) {
        SetError(FriendlyError(hrCom));
        running_ = false;
        return;
    }
    const std::wstring key = InstanceKey(path);

    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    auto capture = [&] {
        {
            ComPtr<IMFAttributes> attrs;
            HRESULT hr = MFCreateAttributes(attrs.GetAddressOf(), 1);
            if (SUCCEEDED(hr))
                hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            IMFActivate** devices = nullptr;
            UINT32 count = 0;
            if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attrs.Get(), &devices, &count);
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
                return;
            }
        }

        {
            ComPtr<IMFAttributes> attrs;
            ComPtr<IMFSourceReaderCallback> callback;
            callback.Attach(new ReaderCallback(state));
            HRESULT hr = MFCreateAttributes(attrs.GetAddressOf(), 2);
            if (SUCCEEDED(hr)) hr = attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
            if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), attrs.Get(), reader.GetAddressOf());
            if (FAILED(hr)) {
                SetError(FriendlyError(hr));
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
            if (best) {
                const HRESULT hr = reader->SetCurrentMediaType(stream, nullptr, best.Get());
                if (FAILED(hr)) { SetError(FriendlyError(hr)); return; }
            }

            UINT32 w = 0, h = 0, num = 0, den = 1;
            GUID sub{};
            if (best) {
                MFGetAttributeSize(best.Get(), MF_MT_FRAME_SIZE, &w, &h);
                MFGetAttributeRatio(best.Get(), MF_MT_FRAME_RATE, &num, &den);
                best->GetGUID(MF_MT_SUBTYPE, &sub);
            }
            ComPtr<IMFMediaType> rgb;
            HRESULT hr = MFCreateMediaType(rgb.GetAddressOf());
            if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, rgb.Get());
            if (FAILED(hr)) {
                SetError(FriendlyError(hr));
                return;
            }
            char fmt[96];
            snprintf(fmt, sizeof fmt, "%u\xC3\x97%u %s %.0f fps", w, h, FourCc(sub).c_str(),
                     den ? static_cast<double>(num) / den : 0.0);
            std::lock_guard<std::mutex> lock(frameMutex_);
            format_ = fmt;
        }

        // Frame geometry of the RGB32 output. Re-read whenever the reader reports a
        // format change, so a new frame size can never overrun the copy below.
        UINT32 width = 0, height = 0;
        LONG defaultStride = 0;
        auto readGeometry = [&] {
            width = height = 0;
            defaultStride = 0;
            ComPtr<IMFMediaType> current;
            if (FAILED(reader->GetCurrentMediaType(stream, current.GetAddressOf())) || !current) return;
            if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
                !safety::Geometry(width, height)) {
                width = height = 0;
                return;
            }
            GUID subtype{};
            if (FAILED(current->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_RGB32) {
                width = height = 0;
                return;
            }
            UINT32 strideAttr = 0;
            if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideAttr)))
                defaultStride = static_cast<LONG>(strideAttr);
            else
                defaultStride = static_cast<LONG>(width * 4);
        };
        readGeometry();
        LOG_INFO("Preview started (%s).", Format().c_str());

        std::vector<uint8_t> scratch;
        while (!stopRequested_) {
            if (!state->BeginRead()) break;
            // All output pointers must be null for an asynchronous SourceReader.
            const HRESULT readHr = reader->ReadSample(stream, 0, nullptr, nullptr, nullptr, nullptr);
            if (FAILED(readHr)) state->Deliver({readHr, 0, {}});
            PreviewReadResult result;
            if (!state->Wait(result)) break;
            if (FAILED(result.status) || (result.flags & MF_SOURCE_READERF_ERROR)) {
                SetError(FriendlyError(FAILED(result.status) ? result.status : E_FAIL));
                break;
            }
            const DWORD flags = result.flags;
            const auto sample = result.sample;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                SetError("The camera stopped sending video.");
                break;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) readGeometry();
            if (!sample || width == 0 || height == 0) continue;
            const size_t rowBytes = static_cast<size_t>(width) * 4;

            scratch.resize(rowBytes * height);
            ComPtr<IMFMediaBuffer> buffer;
            DWORD bufferCount = 0;
            if (FAILED(sample->GetBufferCount(&bufferCount))) continue;
            HRESULT hr = bufferCount == 1 ? sample->GetBufferByIndex(0, buffer.GetAddressOf())
                                         : sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
            if (FAILED(hr)) continue;

            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ComPtr<IMF2DBuffer2> buffer2d;
            bool locked2d = false;
            BYTE* raw = nullptr;
            DWORD allocation = 0;
            uint64_t scanOffset = 0;
            if (SUCCEEDED(buffer.As(&buffer2d)) &&
                SUCCEEDED(buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan0, &pitch, &raw, &allocation))) {
                locked2d = true;
                const uintptr_t start = reinterpret_cast<uintptr_t>(raw);
                const uintptr_t scan = reinterpret_cast<uintptr_t>(scan0);
                if (!raw || !scan0 || scan < start) {
                    buffer2d->Unlock2D();
                    continue;
                }
                scanOffset = static_cast<uint64_t>(scan - start);
            } else {
                DWORD maxLen = 0;
                if (FAILED(buffer->Lock(&raw, &maxLen, &allocation))) continue;
                pitch = defaultStride;
                const uint64_t stride = pitch < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(pitch))
                                                  : static_cast<uint64_t>(pitch);
                scanOffset = pitch < 0 ? stride * (height - 1) : 0;
                if (!raw || allocation > maxLen) {
                    buffer->Unlock();
                    continue;
                }
            }
            if (!safety::FrameBounds(width, height, pitch, scanOffset, allocation)) {
                if (locked2d) buffer2d->Unlock2D(); else buffer->Unlock();
                continue;
            }
            scan0 = raw + static_cast<size_t>(scanOffset);

            for (UINT32 y = 0; y < height; ++y) {
                const uint8_t* src = scan0 + static_cast<ptrdiff_t>(pitch) * y;
                uint8_t* dst = scratch.data() + static_cast<size_t>(y) * rowBytes;
                std::memcpy(dst, src, rowBytes);
                for (UINT32 x = 0; x < width; ++x) dst[x * 4 + 3] = 0xFF;  // RGB32 alpha is undefined
            }
            if (locked2d)
                buffer2d->Unlock2D();
            else
                buffer->Unlock();

            state->IfActive([&] {
                std::lock_guard<std::mutex> lock(frameMutex_);
                frame_.swap(scratch);
                frameW_ = static_cast<int>(width);
                frameH_ = static_cast<int>(height);
                ++frameId_;
            });
        }

    };
    try {
        capture();
    } catch (const std::exception&) {
        SetError("Preview failed: there was not enough memory to capture a frame.");
    }
    state->Cancel();
    if (reader) reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
    reader.Reset();
    if (source) source->Shutdown();
    source.Reset();
    LOG_INFO("Preview stopped.");
    running_ = false;
    if (SUCCEEDED(hrCom)) CoUninitialize();
}

ID3D11ShaderResourceView* Preview::Update(ID3D11Device* device, ID3D11DeviceContext* ctx, int* width, int* height) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (frameId_ == 0) return nullptr;
    if (!upload_.Try(frameId_, [&] {
        if (!texture_ || !srv_ || texW_ != frameW_ || texH_ != frameH_) {
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
            if (FAILED(device->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()))) return false;
            if (FAILED(device->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf()))) return false;
            texW_ = frameW_;
            texH_ = frameH_;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        const size_t rowBytes = static_cast<size_t>(frameW_) * 4;
        if (!mapped.pData || mapped.RowPitch < rowBytes) {
            ctx->Unmap(texture_.Get(), 0);
            return false;
        }
        for (int y = 0; y < frameH_; ++y)
            std::memcpy(static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(mapped.RowPitch) * y,
                        frame_.data() + rowBytes * y, rowBytes);
        ctx->Unmap(texture_.Get(), 0);
        return true;
    })) return nullptr;
    *width = texW_;
    *height = texH_;
    return srv_.Get();
}
