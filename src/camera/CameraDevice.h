// CameraDevice: finds OBSBOT cameras via DirectShow and talks to one of them
// through standard UVC controls (IAMCameraControl / IAMVideoProcAmp) and the
// vendor extension unit (IKsTopologyInfo + IKsControl).
// All methods must be called from one COM-initialised thread (CameraWorker).
#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "ObsbotProtocol.h"

struct IBaseFilter;
struct IAMCameraControl;
struct IAMVideoProcAmp;
struct IKsControlCompat;

struct DeviceInfo {
    std::wstring path;  // DirectShow device path, e.g. \\?\usb#vid_3564&pid_fef9&mi_00#...
    std::string name;   // friendly name reported by Windows
    uint16_t vid = 0;
    uint16_t pid = 0;
    obsbot::Model model = obsbot::Model::Unknown;
};

// Lists video capture devices whose USB vendor ID is OBSBOT's (0x3564).
std::vector<DeviceInfo> EnumerateObsbotCameras();

// Standard UVC controls exposed by DirectShow.
enum class UvcCtl {
    Pan, Tilt, Zoom, Exposure, Focus,
    Brightness, Contrast, Hue, Saturation, Sharpness, Gamma, WhiteBalance,
    BacklightComp, Gain, PowerLine,
    Count
};

struct UvcInfo {
    bool supported = false;
    long min = 0, max = 0, step = 1, def = 0;
    bool canAuto = false;
    bool canManual = true;
    long value = 0;
    bool isAuto = false;
};

// Anti-flicker values for UvcCtl::PowerLine (UVC power line frequency).
constexpr long kPowerLineDisabled = 0;
constexpr long kPowerLine50Hz = 1;
constexpr long kPowerLine60Hz = 2;

class CameraDevice {
public:
    CameraDevice();
    ~CameraDevice();
    CameraDevice(const CameraDevice&) = delete;
    CameraDevice& operator=(const CameraDevice&) = delete;

    bool Open(const DeviceInfo& info);
    void Close();
    bool IsOpen() const { return filter_ != nullptr; }
    const DeviceInfo& Info() const { return info_; }

    bool QueryUvc(UvcCtl c, UvcInfo* out);
    bool GetUvc(UvcCtl c, long* value, bool* isAuto);
    HRESULT SetUvc(UvcCtl c, long value, bool isAuto);

    bool HasXu() const { return xuFound_; }
    unsigned long XuNode() const { return xuNode_; }
    HRESULT XuSet(uint32_t selector, const obsbot::XuBuffer& data);
    HRESULT XuGet(uint32_t selector, obsbot::XuBuffer* data);

private:
    bool FindXuNode();
    HRESULT XuTransfer(uint32_t selector, unsigned long flags, unsigned long node, void* data, unsigned long len);

    DeviceInfo info_;
    Microsoft::WRL::ComPtr<IBaseFilter> filter_;
    Microsoft::WRL::ComPtr<IAMCameraControl> cameraControl_;
    Microsoft::WRL::ComPtr<IAMVideoProcAmp> procAmp_;
    Microsoft::WRL::ComPtr<IKsControlCompat> ksControl_;
    bool xuFound_ = false;
    unsigned long xuNode_ = 0;
};
