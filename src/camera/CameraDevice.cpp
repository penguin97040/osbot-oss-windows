#include "CameraDevice.h"
#include <algorithm>

#include <dshow.h>
#include <ks.h>
#include <vidcap.h>

#include <cwchar>

#include "../app/Log.h"

using Microsoft::WRL::ComPtr;

// Declared here instead of including <ksproxy.h>, whose prerequisites differ
// between the Windows SDK and MinGW. Only the vtable layout matters.
struct IKsControlCompat : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE KsProperty(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData,
                                                 ULONG DataLength, ULONG* BytesReturned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsMethod(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData,
                                               ULONG DataLength, ULONG* BytesReturned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsEvent(PKSEVENT Event, ULONG EventLength, LPVOID EventData,
                                              ULONG DataLength, ULONG* BytesReturned) = 0;
};

namespace {

// OBSBOT vendor extension unit GUID {9A1E7291-6843-4683-6D92-39BC7906EE49}.
const GUID kObsbotXuGuid = {0x9A1E7291, 0x6843, 0x4683, {0x6D, 0x92, 0x39, 0xBC, 0x79, 0x06, 0xEE, 0x49}};
// KSNODETYPE_DEV_SPECIFIC {941C7AC0-C559-11D0-8A2B-00A0C9255AC1}.
const GUID kNodeTypeDevSpecific = {0x941C7AC0, 0xC559, 0x11D0, {0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1}};
const IID kIidIKsControl = {0x28F54685, 0x06FD, 0x11D2, {0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
const IID kIidIKsTopologyInfo = {0x720D4AC0, 0x7533, 0x11D0, {0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00}};

constexpr long kProcAmpPowerLineFrequency = 13;  // KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY

struct UvcMapping {
    bool isCameraControl;
    long property;
};

UvcMapping MapUvc(UvcCtl c) {
    switch (c) {
    case UvcCtl::Pan: return {true, CameraControl_Pan};
    case UvcCtl::Tilt: return {true, CameraControl_Tilt};
    case UvcCtl::Zoom: return {true, CameraControl_Zoom};
    case UvcCtl::Exposure: return {true, CameraControl_Exposure};
    case UvcCtl::Focus: return {true, CameraControl_Focus};
    case UvcCtl::Brightness: return {false, VideoProcAmp_Brightness};
    case UvcCtl::Contrast: return {false, VideoProcAmp_Contrast};
    case UvcCtl::Hue: return {false, VideoProcAmp_Hue};
    case UvcCtl::Saturation: return {false, VideoProcAmp_Saturation};
    case UvcCtl::Sharpness: return {false, VideoProcAmp_Sharpness};
    case UvcCtl::Gamma: return {false, VideoProcAmp_Gamma};
    case UvcCtl::WhiteBalance: return {false, VideoProcAmp_WhiteBalance};
    case UvcCtl::BacklightComp: return {false, VideoProcAmp_BacklightCompensation};
    case UvcCtl::Gain: return {false, VideoProcAmp_Gain};
    case UvcCtl::PowerLine: return {false, kProcAmpPowerLineFrequency};
    default: return {false, -1};
    }
}

std::string Narrow(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// Finds "vid_XXXX" / "pid_XXXX" (case-insensitive) in a device path.
uint16_t ParseUsbId(const std::wstring& path, const wchar_t* key) {
    std::wstring lower = path;
    for (auto& ch : lower) ch = static_cast<wchar_t>(towlower(ch));
    size_t pos = lower.find(key);
    if (pos == std::wstring::npos || pos + 8 > lower.size()) return 0;
    return static_cast<uint16_t>(wcstoul(lower.substr(pos + 4, 4).c_str(), nullptr, 16));
}

std::wstring ReadBagString(IPropertyBag* bag, const wchar_t* name) {
    VARIANT v;
    VariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(bag->Read(name, &v, nullptr)) && v.vt == VT_BSTR && v.bstrVal) out = v.bstrVal;
    VariantClear(&v);
    return out;
}

// Calls fn(moniker, path, friendlyName) for every video input device.
template <typename Fn>
void ForEachVideoDevice(Fn fn) {
    ComPtr<ICreateDevEnum> devEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                                  reinterpret_cast<void**>(devEnum.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("Could not create the device enumerator: %s", logx::HrText(hr).c_str());
        return;
    }
    ComPtr<IEnumMoniker> monikers;
    hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, monikers.GetAddressOf(), 0);
    if (hr != S_OK) return;  // S_FALSE: no video devices at all
    ComPtr<IMoniker> moniker;
    while (monikers->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        ComPtr<IPropertyBag> bag;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                          reinterpret_cast<void**>(bag.GetAddressOf()))))
            continue;
        std::wstring path = ReadBagString(bag.Get(), L"DevicePath");
        std::wstring name = ReadBagString(bag.Get(), L"FriendlyName");
        if (!fn(moniker.Get(), path, name)) break;
    }
}

}  // namespace

std::vector<DeviceInfo> EnumerateObsbotCameras() {
    std::vector<DeviceInfo> out;
    ForEachVideoDevice([&](IMoniker*, const std::wstring& path, const std::wstring& name) {
        uint16_t vid = ParseUsbId(path, L"vid_");
        if (vid != obsbot::kVendorId) return true;
        DeviceInfo d;
        d.path = path;
        d.name = Narrow(name.c_str());
        d.vid = vid;
        d.pid = ParseUsbId(path, L"pid_");
        d.model = obsbot::ModelFromPid(d.pid);
        out.push_back(d);
        return true;
    });
    return out;
}

CameraDevice::CameraDevice() = default;
CameraDevice::~CameraDevice() { Close(); }

bool CameraDevice::Open(const DeviceInfo& info) {
    Close();
    ForEachVideoDevice([&](IMoniker* moniker, const std::wstring& path, const std::wstring&) {
        if (_wcsicmp(path.c_str(), info.path.c_str()) != 0) return true;
        HRESULT hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                                           reinterpret_cast<void**>(filter_.GetAddressOf()));
        if (FAILED(hr)) LOG_ERROR("Could not open the camera: %s", logx::HrText(hr).c_str());
        return false;
    });
    if (!filter_) return false;
    info_ = info;
    filter_->QueryInterface(IID_IAMCameraControl, reinterpret_cast<void**>(cameraControl_.GetAddressOf()));
    filter_->QueryInterface(IID_IAMVideoProcAmp, reinterpret_cast<void**>(procAmp_.GetAddressOf()));
    filter_->QueryInterface(kIidIKsControl, reinterpret_cast<void**>(ksControl_.GetAddressOf()));
    if (!cameraControl_) LOG_WARN("Camera has no IAMCameraControl (pan/tilt/zoom unavailable).");
    if (!procAmp_) LOG_WARN("Camera has no IAMVideoProcAmp (image controls unavailable).");
    xuFound_ = FindXuNode();
    if (xuFound_)
        LOG_INFO("Found the OBSBOT extension unit on KS node %lu.", xuNode_);
    else
        LOG_WARN("OBSBOT extension unit not found: AI tracking, HDR, FOV and sleep controls are unavailable.");
    return true;
}

void CameraDevice::Close() {
    ksControl_.Reset();
    procAmp_.Reset();
    cameraControl_.Reset();
    filter_.Reset();
    xuFound_ = false;
    xuNode_ = 0;
}

bool CameraDevice::FindXuNode() {
    if (!ksControl_) return false;
    ComPtr<IKsTopologyInfo> topo;
    if (FAILED(filter_->QueryInterface(kIidIKsTopologyInfo, reinterpret_cast<void**>(topo.GetAddressOf()))))
        return false;
    DWORD nodes = 0;
    if (FAILED(topo->get_NumNodes(&nodes))) return false;
    for (DWORD i = 0; i < nodes; ++i) {
        GUID type{};
        if (FAILED(topo->get_NodeType(i, &type)) || type != kNodeTypeDevSpecific) continue;
        // Probe: a harmless GET of the status block only succeeds on our XU.
        obsbot::XuBuffer probe{};
        if (SUCCEEDED(XuTransfer(obsbot::kSelectorSimple, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, i,
                                 probe.data(), static_cast<unsigned long>(probe.size())))) {
            xuNode_ = i;
            return true;
        }
    }
    return false;
}

HRESULT CameraDevice::XuTransfer(uint32_t selector, unsigned long flags, unsigned long node, void* data,
                                 unsigned long len) {
    if (!ksControl_) return E_NOINTERFACE;
    KSP_NODE prop{};
    prop.Property.Set = kObsbotXuGuid;
    prop.Property.Id = selector;
    prop.Property.Flags = flags;
    prop.NodeId = node;
    ULONG returned = 0;
    const HRESULT hr = ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&prop), sizeof prop, data, len, &returned);
    if (SUCCEEDED(hr) && (flags & KSPROPERTY_TYPE_GET) && returned != len)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return hr;
}

HRESULT CameraDevice::XuSet(uint32_t selector, const obsbot::XuBuffer& data) {
    if (!xuFound_) return E_NOINTERFACE;
    obsbot::XuBuffer copy = data;
    return XuTransfer(selector, KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY, xuNode_, copy.data(),
                      static_cast<unsigned long>(copy.size()));
}

HRESULT CameraDevice::XuGet(uint32_t selector, obsbot::XuBuffer* data) {
    if (!xuFound_) return E_NOINTERFACE;
    data->fill(0);
    return XuTransfer(selector, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY, xuNode_, data->data(),
                      static_cast<unsigned long>(data->size()));
}

bool CameraDevice::QueryUvc(UvcCtl c, UvcInfo* out) {
    *out = UvcInfo{};
    UvcMapping m = MapUvc(c);
    long min = 0, max = 0, step = 0, def = 0, caps = 0;
    HRESULT hr = E_NOINTERFACE;
    if (m.isCameraControl && cameraControl_)
        hr = cameraControl_->GetRange(m.property, &min, &max, &step, &def, &caps);
    else if (!m.isCameraControl && procAmp_)
        hr = procAmp_->GetRange(m.property, &min, &max, &step, &def, &caps);
    if (FAILED(hr) || max < min) return false;
    out->supported = true;
    out->min = min;
    out->max = max;
    out->step = step > 0 ? step : 1;
    out->def = def;
    // CameraControl_Flags_Auto == VideoProcAmp_Flags_Auto == 1, _Manual == 2.
    out->canAuto = (caps & 0x1) != 0;
    out->canManual = (caps & 0x2) != 0 || caps == 0;
    GetUvc(c, &out->value, &out->isAuto);
    return true;
}

bool CameraDevice::GetUvc(UvcCtl c, long* value, bool* isAuto) {
    UvcMapping m = MapUvc(c);
    long v = 0, flags = 0;
    HRESULT hr = E_NOINTERFACE;
    if (m.isCameraControl && cameraControl_)
        hr = cameraControl_->Get(m.property, &v, &flags);
    else if (!m.isCameraControl && procAmp_)
        hr = procAmp_->Get(m.property, &v, &flags);
    if (FAILED(hr)) return false;
    *value = v;
    *isAuto = (flags & 0x1) != 0;
    return true;
}

HRESULT CameraDevice::SetUvc(UvcCtl c, long value, bool isAuto) {
    if (c < UvcCtl::Pan || c >= UvcCtl::Count) return E_INVALIDARG;
    UvcInfo range;
    if (!QueryUvc(c, &range)) return E_NOINTERFACE;
    if ((isAuto && !range.canAuto) || (!isAuto && !range.canManual)) return E_INVALIDARG;
    value = std::clamp(value, range.min, range.max);
    UvcMapping m = MapUvc(c);
    long flags = isAuto ? 0x1 : 0x2;
    if (m.isCameraControl && cameraControl_) return cameraControl_->Set(m.property, value, flags);
    if (!m.isCameraControl && procAmp_) return procAmp_->Set(m.property, value, flags);
    return E_NOINTERFACE;
}
