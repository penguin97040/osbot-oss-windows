// CameraWorker: owns the CameraDevice on a background COM thread. The UI posts
// commands (never blocks) and reads a copied CameraState snapshot each frame.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CameraDevice.h"
#include "../Safety.h"
#include "ObsbotProtocol.h"

// Which protocol codes to use where the sources disagree (see docs/PROTOCOL.md).
enum class VariantSetting { Auto = 0, Tiny2 = 1, Lite = 2 };

struct CameraState {
    bool connected = false;
    DeviceInfo device;
    std::vector<DeviceInfo> available;
    bool xu = false;
    unsigned long xuNode = 0;
    obsbot::Variant variant = obsbot::Variant::Tiny2;
    std::array<UvcInfo, static_cast<size_t>(UvcCtl::Count)> uvc{};
    obsbot::Status status;
    obsbot::XuBuffer rawStatus{};
    std::string devOutput;  // last Developer-tab result
};

class CameraWorker {
public:
    CameraWorker();
    ~CameraWorker();

    void Start();
    void Stop();
    CameraState Snapshot();

    // Settings that affect behaviour; safe to call any time.
    void SetVariantSetting(VariantSetting v);
    void SetAntiFlickerOnConnect(long powerLineValue);  // -1 = leave the camera alone
    void SelectDevice(const std::wstring& path);

    // Asynchronous commands.
    void SetUvc(UvcCtl c, long value, bool isAuto);
    void ResetImageDefaults();
    void SetAiMode(obsbot::AiMode m);
    void SetSportTracking(bool sport);
    void SetHdr(bool on);
    void SetFov(obsbot::Fov f);
    void SetSleep(bool sleep);
    void Recentre();
    void GotoPosition(long pan, long tilt, long zoom);
    void RefreshAll();

    // Hold-to-move. Call every UI frame while a direction is held; movement
    // stops automatically if calls stop for longer than the dead-man timeout.
    void HoldGimbalVelocity(float pitchDegPerSec, float yawDegPerSec);

    // Developer tab.
    void DevSetRaw(uint32_t selector, std::vector<uint8_t> bytes);
    void DevGetRaw(uint32_t selector);
    void DevFramed(obsbot::Receiver receiver, uint16_t command, std::vector<uint8_t> payload);

private:
    using Task = std::function<void()>;
    void Post(Task task, const std::string& coalesceKey = {});
    void Run();
    void TryConnect();
    bool Disconnect(const char* reason, bool requireStop = false);
    void ClearGimbalHold();
    bool StopGimbal();
    bool SendGimbalStop();
    void QueryAllUvc();
    void ReadStatus();
    void ServiceGimbal();
    void Pause(DWORD ms);
    bool SendFramed(const char* what, obsbot::Receiver receiver, uint16_t command, const uint8_t* payload,
                    size_t len, bool waitForReply, obsbot::FrameReply* reply = nullptr);
    bool SendSimple(const char* what, const obsbot::XuBuffer& buf);
    obsbot::Variant EffectiveVariant() const;
    void UpdateState(const std::function<void(CameraState&)>& fn);

    CameraDevice device_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::pair<std::string, Task>> queue_;

    std::mutex stateMutex_;
    CameraState state_;

    std::atomic<int> variantSetting_{0};
    std::atomic<long> antiFlicker_{-1};
    std::mutex prefMutex_;
    std::wstring preferredPath_;
    uint64_t selectionGeneration_ = 0;

    // Gimbal hold-to-move (written by UI thread, read by worker).
    std::mutex gimbalMutex_;
    float wantPitch_ = 0, wantYaw_ = 0;
    std::chrono::steady_clock::time_point gimbalRefreshed_{};
    safety::GimbalMotion gimbal_;
    std::atomic<bool> switching_{false};
    std::chrono::steady_clock::time_point lastGimbalSend_{};

    uint16_t seq_ = 1;
};
