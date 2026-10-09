#include "ObsbotProtocol.h"

#include <cctype>
#include <cstring>

namespace obsbot {

Model ModelFromPid(uint16_t pid) {
    switch (pid) {
    case kPidTiny2: return Model::Tiny2;
    case kPidTiny2Lite: return Model::Tiny2Lite;
    default: return Model::Unknown;
    }
}

const char* ModelName(Model m) {
    switch (m) {
    case Model::Tiny2: return "OBSBOT Tiny 2";
    case Model::Tiny2Lite: return "OBSBOT Tiny 2 Lite";
    default: return "Unknown OBSBOT camera";
    }
}

uint16_t Crc16Usb(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
    }
    return static_cast<uint16_t>(crc ^ 0xFFFF);
}

static void PutU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

static uint16_t GetU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

XuBuffer BuildSimple(std::initializer_list<uint8_t> bytes) {
    XuBuffer buf{};
    size_t i = 0;
    for (uint8_t b : bytes) {
        if (i >= buf.size()) break;
        buf[i++] = b;
    }
    return buf;
}

// ---- AI modes ---------------------------------------------------------------

struct AiModeBytes { AiMode mode; uint8_t m, n; const char* name; };

static const AiModeBytes kAiModes[] = {
    {AiMode::Off,        0x00, 0x00, "Off"},
    {AiMode::Normal,     0x02, 0x00, "Normal (whole body)"},
    {AiMode::UpperBody,  0x02, 0x01, "Upper body"},
    {AiMode::CloseUp,    0x02, 0x02, "Close-up"},
    {AiMode::Headless,   0x02, 0x03, "Headless"},
    {AiMode::LowerBody,  0x02, 0x04, "Lower body"},
    {AiMode::Group,      0x01, 0x00, "Group"},
    {AiMode::Hand,       0x03, 0x00, "Hand"},
    {AiMode::Whiteboard, 0x04, 0x00, "Whiteboard"},
    {AiMode::Desk,       0x05, 0x00, "Desk"},
};

const char* AiModeName(AiMode m) {
    for (const auto& e : kAiModes)
        if (e.mode == m) return e.name;
    return "?";
}

XuBuffer AiModeCommand(AiMode m) {
    for (const auto& e : kAiModes)
        if (e.mode == m) return BuildSimple({0x16, 0x02, e.m, e.n});
    return BuildSimple({0x16, 0x02, 0x00, 0x00});
}

AiMode AiModeFromBytes(uint8_t m, uint8_t n, bool* known) {
    for (const auto& e : kAiModes) {
        if (e.m == m && e.n == n) {
            if (known) *known = true;
            return e.mode;
        }
    }
    if (known) *known = false;
    return AiMode::Off;
}

// ---- Other simple commands -------------------------------------------------

XuBuffer HdrCommand(bool on) {
    return BuildSimple({0x01, 0x01, static_cast<uint8_t>(on ? 1 : 0)});
}

const char* FovName(Fov f) {
    switch (f) {
    case Fov::Wide: return "Wide";
    case Fov::Medium: return "Medium";
    case Fov::Narrow: return "Narrow";
    default: return "?";
    }
}

static uint8_t FovCode(Fov f, Variant v) {
    const uint8_t base = (v == Variant::Lite) ? 1 : 0;
    return static_cast<uint8_t>(base + static_cast<uint8_t>(f));
}

XuBuffer FovCommand(Fov f, Variant v) {
    return BuildSimple({0x04, 0x01, FovCode(f, v)});
}

bool FovFromStatus(uint8_t raw, Variant v, Fov* out) {
    for (int i = 0; i < static_cast<int>(Fov::Count); ++i) {
        if (FovCode(static_cast<Fov>(i), v) == raw) {
            *out = static_cast<Fov>(i);
            return true;
        }
    }
    return false;
}

XuBuffer SimpleSleepCommand(bool sleep) {
    return BuildSimple({0x02, 0x01, static_cast<uint8_t>(sleep ? 1 : 0)});
}

XuBuffer SimpleRecentreCommand() {
    return BuildSimple({0x16, 0x01, 0x00, 0x00});
}

// ---- Framed commands ---------------------------------------------------------

XuBuffer BuildFrame(uint16_t seq, Receiver receiver, uint16_t command,
                    const uint8_t* payload, size_t payloadLen) {
    XuBuffer f{};
    if (payloadLen > kXuLength - 16 || (payloadLen && !payload)) return f;
    f[0] = 0xAA;
    f[1] = payloadLen ? kFlagSet : kFlagGet;
    PutU16(&f[2], seq);
    PutU16(&f[4], 0x000C);
    f[8] = kHostId;
    f[9] = static_cast<uint8_t>(receiver);
    PutU16(&f[10], command);
    if (payloadLen) {
        PutU16(&f[12], static_cast<uint16_t>(payloadLen));
        std::memcpy(&f[16], payload, payloadLen);
        // Payload CRC covers [12, 16 + len) with its own slot (14-15) zeroed.
        PutU16(&f[14], Crc16Usb(&f[12], 4 + payloadLen));
    }
    // Header CRC covers [0, 12) with its own slot (6-7) zeroed.
    PutU16(&f[6], Crc16Usb(&f[0], 12));
    return f;
}

static void PutF32(uint8_t* p, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(bits >> (8 * i));
}

XuBuffer SleepWakeFrame(uint16_t seq, bool sleep) {
    const uint8_t payload[4] = {static_cast<uint8_t>(sleep ? 1 : 0), 0, 0, 0};
    return BuildFrame(seq, Receiver::Camera, cmd::kSleepWake, payload, sizeof payload);
}

XuBuffer RecentreFrame(uint16_t seq) {
    const uint8_t payload[6] = {};
    return BuildFrame(seq, Receiver::Gimbal, cmd::kRecentre, payload, sizeof payload);
}

std::array<uint8_t, 12> GimbalSpeedPayload(float pitchDegPerSec, float yawDegPerSec) {
    std::array<uint8_t, 12> payload{};
    PutF32(&payload[0], 0.0f);  // roll
    PutF32(&payload[4], pitchDegPerSec);
    PutF32(&payload[8], yawDegPerSec);
    return payload;
}

XuBuffer GimbalSpeedFrame(uint16_t seq, float pitchDegPerSec, float yawDegPerSec) {
    const auto payload = GimbalSpeedPayload(pitchDegPerSec, yawDegPerSec);
    return BuildFrame(seq, Receiver::Ai, cmd::kGimbalSpeed, payload.data(), payload.size());
}

XuBuffer TrackingSpeedFrame(uint16_t seq, bool sport) {
    const uint8_t payload[1] = {static_cast<uint8_t>(sport ? 2 : 0)};
    return BuildFrame(seq, Receiver::Ai, cmd::kTrackingSpeed, payload, sizeof payload);
}

FrameReply ParseFrame(const XuBuffer& buf) {
    FrameReply r;
    if (buf[0] != 0xAA) return r;
    XuBuffer copy = buf;
    copy[6] = copy[7] = 0;
    if (Crc16Usb(copy.data(), 12) != GetU16(&buf[6])) return r;
    const uint16_t len = GetU16(&buf[12]);
    if (len > kXuLength - 16) return r;
    if (len) {
        copy[14] = copy[15] = 0;
        if (Crc16Usb(&copy[12], 4 + len) != GetU16(&buf[14])) return r;
    }
    r.valid = true;
    r.flags = buf[1];
    r.seq = GetU16(&buf[2]);
    r.command = GetU16(&buf[10]);
    if (len > 0)
        r.payload.assign(buf.begin() + 16, buf.begin() + 16 + len);
    return r;
}

// ---- Status block ------------------------------------------------------------

Status ParseStatus(const XuBuffer& buf) {
    Status s;
    bool anyNonZero = false;
    for (uint8_t b : buf) anyNonZero |= (b != 0);
    if (!anyNonZero) return s;
    s.valid = true;
    s.deviceStatus = buf[0x09];
    s.asleep = buf[0x02] == 1 || s.deviceStatus == 3;
    s.zoomPercent = GetU16(&buf[0x04]);
    s.hdr = buf[0x06] != 0;
    s.fovRaw = buf[0x11];
    s.aiM = buf[0x18];
    s.aiN = buf[0x1C];
    s.aiMode = AiModeFromBytes(s.aiM, s.aiN, &s.aiModeKnown);
    s.sportTracking = buf[0x24] == 2;
    return s;
}

// ---- Hex helpers ---------------------------------------------------------------

std::string ToHex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; ++i) {
        if (i) out += ' ';
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xF];
    }
    return out;
}

bool ParseHex(const std::string& text, std::vector<uint8_t>* out) {
    out->clear();
    int nibble = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            if (nibble >= 0) return false;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ':' || c == '-') {
            if (nibble >= 0) { out->push_back(static_cast<uint8_t>(nibble)); nibble = -1; }
            continue;
        }
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return false;
        if (nibble < 0) {
            nibble = v;
        } else {
            out->push_back(static_cast<uint8_t>((nibble << 4) | v));
            nibble = -1;
        }
    }
    if (nibble >= 0) out->push_back(static_cast<uint8_t>(nibble));
    return true;
}

}  // namespace obsbot
