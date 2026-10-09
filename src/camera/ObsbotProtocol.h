// ObsbotProtocol: platform-independent encoding/decoding of the OBSBOT UVC
// extension-unit (XU) messages. No Windows headers here so it can be unit
// tested on Linux. See docs/PROTOCOL.md for the sources of every constant.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace obsbot {

constexpr uint16_t kVendorId = 0x3564;
constexpr uint16_t kPidTiny2 = 0xFEF8;
constexpr uint16_t kPidTiny2Lite = 0xFEF9;

// Every XU control on unit 2 transfers exactly 60 bytes.
constexpr size_t kXuLength = 60;
using XuBuffer = std::array<uint8_t, kXuLength>;

// XU selectors (control IDs) on the vendor extension unit.
constexpr uint32_t kSelectorFramed = 2;  // CRC-framed "V3" commands + reply mailbox
constexpr uint32_t kSelectorSimple = 6;  // simple tag commands (SET) / status block (GET)
constexpr uint32_t kSelectorProductName = 8;

enum class Model { Unknown, Tiny2, Tiny2Lite };
Model ModelFromPid(uint16_t pid);
const char* ModelName(Model m);

// Which byte codes to use where the published sources disagree.
// Tiny2 = lxman/obsbot-mcp + cgevans/tiny2 (tested on Tiny 2).
// Lite  = me-tony/nod (tested on Tiny 2 Lite).
enum class Variant { Tiny2, Lite };

// CRC-16/USB: poly 0x8005 reflected (0xA001), init 0xFFFF, xorout 0xFFFF.
uint16_t Crc16Usb(const uint8_t* data, size_t len);

// ---- Selector 6: simple tag commands, zero padded, no CRC ----------------
XuBuffer BuildSimple(std::initializer_list<uint8_t> bytes);

enum class AiMode {
    Off, Normal, UpperBody, CloseUp, Headless, LowerBody, Group, Hand, Whiteboard, Desk,
    Count
};
const char* AiModeName(AiMode m);
XuBuffer AiModeCommand(AiMode m);
AiMode AiModeFromBytes(uint8_t m, uint8_t n, bool* known = nullptr);

XuBuffer HdrCommand(bool on);

enum class Fov { Wide, Medium, Narrow, Count };
const char* FovName(Fov f);
XuBuffer FovCommand(Fov f, Variant v);

XuBuffer SimpleSleepCommand(bool sleep);  // Lite variant only
XuBuffer SimpleRecentreCommand();         // Lite variant only

// ---- Selector 2: CRC-framed "V3" commands --------------------------------
enum class Receiver : uint8_t { Camera = 0x02, Gimbal = 0x03, Ai = 0x04 };

namespace cmd {
constexpr uint16_t kSleepWake = 0xA0C2;     // Camera, u32: 0 wake, 1 sleep
constexpr uint16_t kRecentre = 0x00C3;      // Gimbal, 6 zero bytes
constexpr uint16_t kGimbalSpeed = 0x6484;   // Ai, 3x f32 [roll, pitch, yaw] deg/s
constexpr uint16_t kTrackingSpeed = 0x0CC4; // Ai, u8: 0 standard, 2 sport
}  // namespace cmd

constexpr uint8_t kFlagSet = 0x25;
constexpr uint8_t kFlagGet = 0x01;
constexpr uint8_t kHostId = 0x0A;

// Builds a framed command. An empty payload makes a header-only GET frame.
// Invalid or oversized payloads return an all-zero buffer.
XuBuffer BuildFrame(uint16_t seq, Receiver receiver, uint16_t command,
                    const uint8_t* payload, size_t payloadLen);
XuBuffer SleepWakeFrame(uint16_t seq, bool sleep);
XuBuffer RecentreFrame(uint16_t seq);
std::array<uint8_t, 12> GimbalSpeedPayload(float pitchDegPerSec, float yawDegPerSec);
XuBuffer GimbalSpeedFrame(uint16_t seq, float pitchDegPerSec, float yawDegPerSec);
XuBuffer TrackingSpeedFrame(uint16_t seq, bool sport);

struct FrameReply {
    bool valid = false;  // magic, header CRC, payload length and payload CRC ok
    uint8_t flags = 0;
    uint16_t seq = 0;
    uint16_t command = 0;
    std::vector<uint8_t> payload;
};
FrameReply ParseFrame(const XuBuffer& buf);

// ---- Status block (GET selector 6) --------------------------------------
struct Status {
    bool valid = false;  // false when the block was all zeros
    bool asleep = false;
    uint8_t deviceStatus = 0;  // 1 run, 3 sleep, 4 privacy (cgevans)
    bool hdr = false;
    uint8_t fovRaw = 0;
    uint16_t zoomPercent = 0;
    uint8_t aiM = 0, aiN = 0;
    AiMode aiMode = AiMode::Off;
    bool aiModeKnown = false;
    bool sportTracking = false;
};
Status ParseStatus(const XuBuffer& buf);
// Maps the raw FOV status byte back to a Fov (returns false for "custom").
bool FovFromStatus(uint8_t raw, Variant v, Fov* out);

// ---- Helpers --------------------------------------------------------------
std::string ToHex(const uint8_t* data, size_t len);
// Parses "aa 25 0x01,ff" style input. Returns false on bad characters.
bool ParseHex(const std::string& text, std::vector<uint8_t>* out);

}  // namespace obsbot
