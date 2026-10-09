#include "CameraWorker.h"

#include <objbase.h>

#include <algorithm>

#include "../app/Log.h"

using namespace std::chrono;
using obsbot::XuBuffer;

namespace {
constexpr auto kScanInterval = milliseconds(2000);
constexpr auto kStatusInterval = milliseconds(1000);
constexpr auto kPresenceInterval = milliseconds(3000);
constexpr auto kGimbalResend = milliseconds(100);
constexpr auto kGimbalDeadMan = milliseconds(250);
constexpr int kReplyPolls = 8;
constexpr DWORD kReplyPollMs = 50;

const char* UvcLogName(UvcCtl c) {
    static const char* names[] = {"pan", "tilt", "zoom", "exposure", "focus", "brightness", "contrast", "hue",
                                  "saturation", "sharpness", "gamma", "white balance", "backlight compensation",
                                  "gain", "anti-flicker"};
    return names[static_cast<int>(c)];
}
}  // namespace

CameraWorker::CameraWorker() = default;
CameraWorker::~CameraWorker() { Stop(); }

void CameraWorker::Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { Run(); });
}

void CameraWorker::Stop() {
    if (!running_.exchange(false)) return;
    queueCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

CameraState CameraWorker::Snapshot() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

void CameraWorker::UpdateState(const std::function<void(CameraState&)>& fn) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    fn(state_);
}

void CameraWorker::Post(Task task, const std::string& coalesceKey) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (!coalesceKey.empty()) {
            for (auto& entry : queue_) {
                if (entry.first == coalesceKey) {
                    entry.second = std::move(task);
                    return;
                }
            }
        }
        queue_.emplace_back(coalesceKey, std::move(task));
    }
    queueCv_.notify_one();
}

void CameraWorker::SetVariantSetting(VariantSetting v) {
    variantSetting_ = static_cast<int>(v);
    Post([this] { UpdateState([this](CameraState& s) { s.variant = EffectiveVariant(); }); }, "variant");
}

void CameraWorker::SetAntiFlickerOnConnect(long powerLineValue) { antiFlicker_ = powerLineValue; }

void CameraWorker::SelectDevice(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(prefMutex_);
        preferredPath_ = path;
        ++selectionGeneration_;
        switching_ = true;
    }
    ClearGimbalHold();
    Post([] {}, "selection");
}

obsbot::Variant CameraWorker::EffectiveVariant() const {
    switch (static_cast<VariantSetting>(variantSetting_.load())) {
    case VariantSetting::Tiny2: return obsbot::Variant::Tiny2;
    case VariantSetting::Lite: return obsbot::Variant::Lite;
    default:
        return device_.Info().model == obsbot::Model::Tiny2Lite ? obsbot::Variant::Lite : obsbot::Variant::Tiny2;
    }
}

// ---- Thread loop ---------------------------------------------------------------

void CameraWorker::Run() {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCom)) {
        LOG_ERROR("Camera worker COM startup failed: %s", logx::HrText(hrCom).c_str());
        return;
    }
    LOG_INFO("Camera worker started.");
    auto lastScan = steady_clock::now() - kScanInterval;
    auto lastStatus = steady_clock::now();
    auto lastPresence = steady_clock::now();

    while (running_) {
        std::deque<std::pair<std::string, Task>> tasks;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            // Wake often enough to service hold-to-move smoothly.
            queueCv_.wait_for(lock, milliseconds(gimbal_.moving ? 20 : 100),
                              [this] { return !queue_.empty() || !running_; });
            tasks.swap(queue_);
        }
        if (!running_) break;
        if (switching_) {
            uint64_t generation;
            {
                std::lock_guard<std::mutex> lock(prefMutex_);
                generation = selectionGeneration_;
            }
            if (!device_.IsOpen() || Disconnect("switching camera", true)) {
                TryConnect();
                std::lock_guard<std::mutex> lock(prefMutex_);
                // A newer selection made during connection must still be serviced.
                if (selectionGeneration_ == generation) switching_ = false;
            }
            // Queued commands belong to the previous device.
            tasks.clear();
        }
        for (auto& t : tasks) {
            if (!running_) break;
            t.second();
            // Keep the hold-to-move dead-man responsive between queued tasks.
            if (device_.IsOpen()) ServiceGimbal();
        }
        if (!running_) break;

        auto now = steady_clock::now();
        if (!device_.IsOpen()) {
            if (now - lastScan >= kScanInterval) {
                lastScan = now;
                TryConnect();
                lastStatus = lastPresence = steady_clock::now();
            }
            continue;
        }

        ServiceGimbal();

        if (now - lastStatus >= kStatusInterval) {
            lastStatus = now;
            ReadStatus();
            for (UvcCtl c : {UvcCtl::Pan, UvcCtl::Tilt, UvcCtl::Zoom}) {
                long v;
                bool a;
                if (device_.GetUvc(c, &v, &a))
                    UpdateState([&](CameraState& s) { s.uvc[static_cast<size_t>(c)].value = v; });
            }
        }
        if (now - lastPresence >= kPresenceInterval) {
            lastPresence = now;
            bool present = false;
            for (const auto& d : EnumerateObsbotCameras())
                present |= _wcsicmp(d.path.c_str(), device_.Info().path.c_str()) == 0;
            if (!present) Disconnect("camera unplugged");
        }
    }

    ClearGimbalHold();
    if (device_.IsOpen()) {
        for (int attempt = 0; attempt < 3 && !StopGimbal(); ++attempt) Sleep(20);
        if (gimbal_.moving) LOG_WARN("Could not stop the gimbal before closing the camera.");
        device_.Close();
    }
    LOG_INFO("Camera worker stopped.");
    if (SUCCEEDED(hrCom)) CoUninitialize();
}

void CameraWorker::TryConnect() {
    std::vector<DeviceInfo> found = EnumerateObsbotCameras();
    UpdateState([&](CameraState& s) { s.available = found; });
    if (found.empty()) return;

    std::wstring preferred;
    {
        std::lock_guard<std::mutex> lock(prefMutex_);
        preferred = preferredPath_;
    }
    const DeviceInfo* pick = &found.front();
    for (const auto& d : found)
        if (!preferred.empty() && _wcsicmp(d.path.c_str(), preferred.c_str()) == 0) pick = &d;

    LOG_INFO("Connecting to %s (VID %04X, PID %04X)...", pick->name.c_str(), pick->vid, pick->pid);
    if (pick->model == obsbot::Model::Unknown)
        LOG_WARN("This OBSBOT model is untested. Controls may not work.");
    if (!device_.Open(*pick)) return;
    ClearGimbalHold();
    gimbal_.moving = false;

    UpdateState([&](CameraState& s) {
        s.connected = true;
        s.device = *pick;
        s.xu = device_.HasXu();
        s.xuNode = device_.XuNode();
        s.variant = EffectiveVariant();
    });
    QueryAllUvc();

    const long af = antiFlicker_.load();
    if (af >= 0 && af <= kPowerLine60Hz) {
        const UvcInfo info = Snapshot().uvc[static_cast<size_t>(UvcCtl::PowerLine)];  // copy: Snapshot() is a temporary
        if (info.supported && info.value != af) {
            HRESULT hr = device_.SetUvc(UvcCtl::PowerLine, af, false);
            if (SUCCEEDED(hr)) {
                LOG_INFO("Applied anti-flicker setting (%s).", af == kPowerLine50Hz ? "50 Hz" : af == kPowerLine60Hz ? "60 Hz" : "off");
                UpdateState([&](CameraState& s) { s.uvc[static_cast<size_t>(UvcCtl::PowerLine)].value = af; });
            } else {
                LOG_WARN("Could not apply anti-flicker setting: %s", logx::HrText(hr).c_str());
            }
        }
    }
    ReadStatus();
    LOG_INFO("Connected.");
}

bool CameraWorker::Disconnect(const char* reason, bool requireStop) {
    ClearGimbalHold();
    if (requireStop) {
        const bool closed = gimbal_.Close([this] { return SendGimbalStop(); }, [this] { device_.Close(); });
        if (!closed) return false;
    } else {
        StopGimbal();
        device_.Close();
    }
    if (gimbal_.moving) LOG_WARN("Gimbal stop failed; the device is no longer available.");
    LOG_WARN("Disconnected (%s).", reason);
    gimbal_.moving = false;
    UpdateState([](CameraState& s) {
        s.connected = false;
        s.xu = false;
        s.uvc = {};
        s.status = {};
        s.rawStatus = {};
    });
    return true;
}

void CameraWorker::QueryAllUvc() {
    std::array<UvcInfo, static_cast<size_t>(UvcCtl::Count)> infos{};
    for (size_t i = 0; i < infos.size(); ++i) device_.QueryUvc(static_cast<UvcCtl>(i), &infos[i]);
    UpdateState([&](CameraState& s) { s.uvc = infos; });
}

void CameraWorker::ReadStatus() {
    if (!device_.HasXu()) return;
    XuBuffer raw{};
    HRESULT hr = device_.XuGet(obsbot::kSelectorSimple, &raw);
    if (FAILED(hr)) return;
    obsbot::Status st = obsbot::ParseStatus(raw);
    UpdateState([&](CameraState& s) {
        s.rawStatus = raw;
        s.status = st;
    });
}

// Sleeps on the worker thread without starving the gimbal dead-man stop.
void CameraWorker::Pause(DWORD ms) {
    const auto until = steady_clock::now() + milliseconds(ms);
    while (running_ && !switching_ && steady_clock::now() < until) {
        Sleep(20);
        if (device_.IsOpen()) ServiceGimbal();
    }
}

// ---- XU helpers -------------------------------------------------------------------

bool CameraWorker::SendSimple(const char* what, const XuBuffer& buf) {
    HRESULT hr = device_.XuSet(obsbot::kSelectorSimple, buf);
    if (FAILED(hr)) {
        LOG_ERROR("%s failed: %s", what, logx::HrText(hr).c_str());
        return false;
    }
    LOG_INFO("%s: sent.", what);
    return true;
}

bool CameraWorker::SendFramed(const char* what, obsbot::Receiver receiver, uint16_t command, const uint8_t* payload,
                              size_t len, bool waitForReply, obsbot::FrameReply* reply) {
    if (len > obsbot::kXuLength - 16 || (len && !payload)) {
        LOG_WARN("%s rejected: payload is too large or missing.", what);
        return false;
    }
    if (waitForReply) {
        ClearGimbalHold();
        if (!running_ || !StopGimbal()) return false;
    }
    const uint16_t seq = seq_++;
    if (seq_ == 0) seq_ = 1;
    XuBuffer frame = obsbot::BuildFrame(seq, receiver, command, payload, len);
    HRESULT hr = device_.XuSet(obsbot::kSelectorFramed, frame);
    if (FAILED(hr)) {
        LOG_ERROR("%s failed: %s", what, logx::HrText(hr).c_str());
        return false;
    }
    if (!waitForReply) return true;
    for (int i = 0; i < kReplyPolls; ++i) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait_for(lock, milliseconds(kReplyPollMs), [this] { return !running_ || switching_; });
        }
        if (!running_ || switching_) return false;
        XuBuffer raw{};
        if (FAILED(device_.XuGet(obsbot::kSelectorFramed, &raw))) continue;
        obsbot::FrameReply r = obsbot::ParseFrame(raw);
        if (r.valid && r.seq == seq && r.command == command) {
            LOG_INFO("%s: camera acknowledged.", what);
            if (reply) *reply = r;
            return true;
        }
    }
    LOG_WARN("%s: no reply from the camera.", what);
    return false;
}

// ---- Commands -------------------------------------------------------------------------

void CameraWorker::SetUvc(UvcCtl c, long value, bool isAuto) {
    if (c < UvcCtl::Pan || c >= UvcCtl::Count) return;
    const auto info = Snapshot().uvc[static_cast<size_t>(c)];
    if (!info.supported) return;
    value = std::clamp(value, info.min, info.max);
    // Update the snapshot straight away so sliders don't jump back while queued.
    UpdateState([&](CameraState& s) {
        auto& u = s.uvc[static_cast<size_t>(c)];
        u.value = value;
        u.isAuto = isAuto;
    });
    Post(
        [this, c, value, isAuto] {
            if (!device_.IsOpen()) return;
            HRESULT hr = device_.SetUvc(c, value, isAuto);
            if (FAILED(hr))
                LOG_ERROR("Setting %s failed: %s", UvcLogName(c), logx::HrText(hr).c_str());
            if (isAuto || FAILED(hr)) {
                long v;
                bool a;
                if (device_.GetUvc(c, &v, &a))
                    UpdateState([&](CameraState& s) {
                        s.uvc[static_cast<size_t>(c)].value = v;
                        s.uvc[static_cast<size_t>(c)].isAuto = a;
                    });
            }
        },
        "uvc:" + std::to_string(static_cast<int>(c)));
}

void CameraWorker::ResetImageDefaults() {
    Post([this] {
        if (!device_.IsOpen()) return;
        for (UvcCtl c : {UvcCtl::Brightness, UvcCtl::Contrast, UvcCtl::Hue, UvcCtl::Saturation, UvcCtl::Sharpness,
                         UvcCtl::Gamma, UvcCtl::Gain, UvcCtl::BacklightComp, UvcCtl::WhiteBalance,
                         UvcCtl::Exposure, UvcCtl::Focus}) {
            UvcInfo info;
            if (!device_.QueryUvc(c, &info)) continue;
            device_.SetUvc(c, info.def, info.canAuto);
        }
        QueryAllUvc();
        LOG_INFO("Image settings reset to camera defaults.");
    });
}

void CameraWorker::SetAiMode(obsbot::AiMode m) {
    UpdateState([&](CameraState& s) {
        s.status.aiMode = m;
        s.status.aiModeKnown = true;
    });
    Post([this, m] {
        std::string what = std::string("AI tracking \xE2\x86\x92 ") + obsbot::AiModeName(m);
        SendSimple(what.c_str(), obsbot::AiModeCommand(m));
    }, "ai");
}

void CameraWorker::SetSportTracking(bool sport) {
    UpdateState([&](CameraState& s) { s.status.sportTracking = sport; });
    Post([this, sport] {
        const uint8_t payload[1] = {static_cast<uint8_t>(sport ? 2 : 0)};
        SendFramed(sport ? "Tracking speed \xE2\x86\x92 sport" : "Tracking speed \xE2\x86\x92 standard",
                   obsbot::Receiver::Ai, obsbot::cmd::kTrackingSpeed, payload, 1, true);
    }, "trackspeed");
}

void CameraWorker::SetHdr(bool on) {
    UpdateState([&](CameraState& s) { s.status.hdr = on; });
    Post([this, on] { SendSimple(on ? "HDR on" : "HDR off", obsbot::HdrCommand(on)); }, "hdr");
}

void CameraWorker::SetFov(obsbot::Fov f) {
    Post([this, f] {
        const obsbot::Variant v = EffectiveVariant();
        std::string what = std::string("Field of view \xE2\x86\x92 ") + obsbot::FovName(f);
        if (!SendSimple(what.c_str(), obsbot::FovCommand(f, v))) return;
        Pause(300);
        ReadStatus();
        obsbot::Fov readBack;
        const auto st = Snapshot().status;
        if (st.valid && !(obsbot::FovFromStatus(st.fovRaw, v, &readBack) && readBack == f))
            LOG_WARN("Field of view status reads %u. If the picture didn't change, try the other protocol "
                     "variant on the Developer tab.", st.fovRaw);
    }, "fov");
}

void CameraWorker::SetSleep(bool sleep) {
    Post([this, sleep] {
        const uint8_t payload[4] = {static_cast<uint8_t>(sleep ? 1 : 0), 0, 0, 0};
        const char* what = sleep ? "Sleep" : "Wake";
        bool ok;
        if (EffectiveVariant() == obsbot::Variant::Lite) {
            ok = SendSimple(what, obsbot::SimpleSleepCommand(sleep));
        } else {
            ok = SendFramed(what, obsbot::Receiver::Camera, obsbot::cmd::kSleepWake, payload, sizeof payload, true);
            if (!ok && running_ && !switching_ && !gimbal_.moving) {
                LOG_INFO("Trying the alternative %s command...", sleep ? "sleep" : "wake");
                SendSimple(what, obsbot::SimpleSleepCommand(sleep));
            }
        }
        Pause(500);
        ReadStatus();
    }, "sleep");
}

void CameraWorker::Recentre() {
    Post([this] {
        const uint8_t payload[6] = {};
        if (!SendFramed("Centre gimbal", obsbot::Receiver::Gimbal, obsbot::cmd::kRecentre, payload, sizeof payload,
                        true)) {
            if (!running_ || switching_ || gimbal_.moving) return;
            LOG_INFO("Trying the alternative centre command...");
            SendSimple("Centre gimbal", obsbot::SimpleRecentreCommand());
        }
    }, "recentre");
}

void CameraWorker::GotoPosition(long pan, long tilt, long zoom) {
    Post([this, pan, tilt, zoom] {
        if (!device_.IsOpen()) return;
        HRESULT a = device_.SetUvc(UvcCtl::Pan, pan, false);
        HRESULT b = device_.SetUvc(UvcCtl::Tilt, tilt, false);
        HRESULT c = device_.SetUvc(UvcCtl::Zoom, zoom, false);
        if (FAILED(a) || FAILED(b) || FAILED(c))
            LOG_WARN("Preset recall partly failed (pan %s, tilt %s, zoom %s).", SUCCEEDED(a) ? "ok" : "failed",
                     SUCCEEDED(b) ? "ok" : "failed", SUCCEEDED(c) ? "ok" : "failed");
        else
            LOG_INFO("Moved to preset (pan %ld, tilt %ld, zoom %ld).", pan, tilt, zoom);
        // Read back the camera values, including clamping and partial failures.
        QueryAllUvc();
    }, "goto");
}

void CameraWorker::RefreshAll() {
    Post([this] {
        if (!device_.IsOpen()) return;
        QueryAllUvc();
        ReadStatus();
        LOG_INFO("Refreshed camera values.");
    }, "refresh");
}

void CameraWorker::HoldGimbalVelocity(float pitchDegPerSec, float yawDegPerSec) {
    if (switching_) return;
    if (!std::isfinite(pitchDegPerSec) || !std::isfinite(yawDegPerSec)) {
        ClearGimbalHold();
        queueCv_.notify_one();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gimbalMutex_);
        if (switching_) return;
        wantPitch_ = safety::Velocity(pitchDegPerSec);
        wantYaw_ = safety::Velocity(yawDegPerSec);
        gimbalRefreshed_ = steady_clock::now();
    }
    queueCv_.notify_one();
}

void CameraWorker::ServiceGimbal() {
    float pitch, yaw;
    steady_clock::time_point refreshed;
    {
        std::lock_guard<std::mutex> lock(gimbalMutex_);
        pitch = wantPitch_;
        yaw = wantYaw_;
        refreshed = gimbalRefreshed_;
    }
    const auto now = steady_clock::now();
    const bool want = !switching_ && (pitch != 0 || yaw != 0) && now - refreshed < kGimbalDeadMan;
    if (want) {
        if (now - lastGimbalSend_ >= kGimbalResend) {
            lastGimbalSend_ = now;
            const auto payload = obsbot::GimbalSpeedPayload(pitch, yaw);
            if (!gimbal_.moving) LOG_INFO("Gimbal moving (pitch %.0f\xC2\xB0/s, yaw %.0f\xC2\xB0/s).", pitch, yaw);
            gimbal_.moving = SendFramed("Gimbal move", obsbot::Receiver::Ai, obsbot::cmd::kGimbalSpeed,
                                       payload.data(), payload.size(), false) || gimbal_.moving;
        }
    } else if (gimbal_.moving) {
        StopGimbal();
    }
}

void CameraWorker::ClearGimbalHold() {
    std::lock_guard<std::mutex> lock(gimbalMutex_);
    wantPitch_ = wantYaw_ = 0;
    gimbalRefreshed_ = {};
}

bool CameraWorker::StopGimbal() {
    const bool wasMoving = gimbal_.moving;
    const bool stopped = gimbal_.Stop([this] { return SendGimbalStop(); });
    if (wasMoving && stopped) LOG_INFO("Gimbal stopped.");
    return stopped;
}

bool CameraWorker::SendGimbalStop() {
    const uint8_t zeros[12] = {};
    return SendFramed("Gimbal stop", obsbot::Receiver::Ai, obsbot::cmd::kGimbalSpeed,
                      zeros, sizeof zeros, false);
}

// ---- Developer tab ---------------------------------------------------------------------

void CameraWorker::DevSetRaw(uint32_t selector, std::vector<uint8_t> bytes) {
    if (bytes.size() > obsbot::kXuLength) {
        LOG_WARN("Developer SET rejected: at most 60 bytes are allowed.");
        UpdateState([](CameraState& s) { s.devOutput = "SET rejected: at most 60 bytes are allowed."; });
        return;
    }
    Post([this, selector, bytes] {
        XuBuffer buf{};
        std::copy(bytes.begin(), bytes.end(), buf.begin());
        HRESULT hr = device_.XuSet(selector, buf);
        std::string out = "SET selector " + std::to_string(selector) + ": " +
                          (SUCCEEDED(hr) ? std::string("OK") : logx::HrText(hr)) + "\n" +
                          obsbot::ToHex(buf.data(), buf.size());
        LOG_INFO("[dev] %s", out.c_str());
        UpdateState([&](CameraState& s) { s.devOutput = out; });
    });
}

void CameraWorker::DevGetRaw(uint32_t selector) {
    Post([this, selector] {
        XuBuffer buf{};
        HRESULT hr = device_.XuGet(selector, &buf);
        std::string out = "GET selector " + std::to_string(selector) + ": " +
                          (SUCCEEDED(hr) ? std::string("OK") : logx::HrText(hr)) + "\n" +
                          obsbot::ToHex(buf.data(), buf.size());
        LOG_INFO("[dev] %s", out.c_str());
        UpdateState([&](CameraState& s) { s.devOutput = out; });
    });
}

void CameraWorker::DevFramed(obsbot::Receiver receiver, uint16_t command, std::vector<uint8_t> payload) {
    if (payload.size() > obsbot::kXuLength - 16) {
        LOG_WARN("Developer frame rejected: at most 44 payload bytes are allowed.");
        UpdateState([](CameraState& s) { s.devOutput = "Frame rejected: at most 44 payload bytes are allowed."; });
        return;
    }
    Post([this, receiver, command, payload] {
        obsbot::FrameReply reply;
        char what[64];
        snprintf(what, sizeof what, "[dev] framed command 0x%04X", command);
        bool ok = SendFramed(what, receiver, command, payload.data(), payload.size(), true, &reply);
        std::string out = std::string(what) + (ok ? ": reply flags " : ": no reply");
        if (ok) {
            char flags[8];
            snprintf(flags, sizeof flags, "0x%02X", reply.flags);
            out += flags;
            out += "\npayload: " + obsbot::ToHex(reply.payload.data(), reply.payload.size());
        }
        UpdateState([&](CameraState& s) { s.devOutput = out; });
    });
}
