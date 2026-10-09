// Unit tests for the platform-independent protocol layer.
// Builds and runs on Linux (g++) and Windows (MSVC/MinGW) via ctest.
// Expected frames are real USB captures published by cgevans/tiny2 and
// mitchelloharawild/obsbot-tiny-2-control (see docs/PROTOCOL.md).
#include "../src/camera/ObsbotProtocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void CheckPrefix(const obsbot::XuBuffer& actual, const char* expectedHex, const char* name) {
    std::vector<uint8_t> expected;
    CHECK(obsbot::ParseHex(expectedHex, &expected));
    bool ok = std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
    for (size_t i = expected.size(); i < actual.size(); ++i) ok = ok && actual[i] == 0;
    if (!ok) {
        std::printf("FAIL %s\n  expected %s\n  actual   %s\n", name, expectedHex,
                    obsbot::ToHex(actual.data(), expected.size()).c_str());
        ++g_failures;
    }
}

int main() {
    using namespace obsbot;

    // CRC-16/USB standard check value.
    CHECK(Crc16Usb(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xB4C8);

    // Captured framed commands.
    CheckPrefix(SleepWakeFrame(0xA5, false),
                "aa 25 a5 00 0c 00 5f ef 0a 02 c2 a0 04 00 be 07 00 00 00 00", "wake seq a5");
    CheckPrefix(SleepWakeFrame(0xAC, false),
                "aa 25 ac 00 0c 00 8f c0 0a 02 c2 a0 04 00 be 07 00 00 00 00", "wake seq ac");
    CheckPrefix(SleepWakeFrame(0xAB, true),
                "aa 25 ab 00 0c 00 3e 1a 0a 02 c2 a0 04 00 bf fb 01 00 00 00", "sleep seq ab");
    {
        const uint8_t autoExp[5] = {0x02, 0x04, 0x00, 0x00, 0x00};
        CheckPrefix(BuildFrame(0x16, Receiver::Camera, 0x2982, autoExp, sizeof autoExp),
                    "aa 25 16 00 0c 00 58 91 0a 02 82 29 05 00 b2 af 02 04", "auto exposure");
        const uint8_t manualExp[5] = {0x01, 0x32, 0x00, 0x00, 0x00};
        CheckPrefix(BuildFrame(0x15, Receiver::Camera, 0x2982, manualExp, sizeof manualExp),
                    "aa 25 15 00 0c 00 a8 9e 0a 02 82 29 05 00 f9 27 01 32", "manual exposure");
    }

    // Header-only GET frame uses flag 0x01 and no payload length.
    {
        XuBuffer f = BuildFrame(7, Receiver::Camera, 0x2942, nullptr, 0);
        CHECK(f[1] == kFlagGet);
        CHECK(f[12] == 0 && f[13] == 0);
        FrameReply r = ParseFrame(f);
        CHECK(r.valid && r.seq == 7 && r.command == 0x2942 && r.payload.empty());
    }

    // Round trip a framed command through the parser.
    {
        FrameReply r = ParseFrame(GimbalSpeedFrame(0x1234, 10.0f, -20.0f));
        CHECK(r.valid);
        CHECK(r.seq == 0x1234);
        CHECK(r.command == cmd::kGimbalSpeed);
        CHECK(r.payload.size() == 12);
        XuBuffer corrupt = GimbalSpeedFrame(1, 0, 0);
        corrupt[10] ^= 0xFF;
        CHECK(!ParseFrame(corrupt).valid);
    }

    // Payload integrity and length, independent of a valid header CRC.
    {
        XuBuffer frame = SleepWakeFrame(0xA5, false);
        CHECK(ParseFrame(frame).valid);
        frame[16] ^= 1;
        CHECK(!ParseFrame(frame).valid);
        frame = SleepWakeFrame(0xA5, false);
        frame[14] ^= 1;
        CHECK(!ParseFrame(frame).valid);
        frame = SleepWakeFrame(0xA5, false);
        frame[12] = 45;
        CHECK(!ParseFrame(frame).valid);
        frame[12] = frame[13] = 0xFF;
        CHECK(!ParseFrame(frame).valid);
        std::vector<uint8_t> payload(44, 0xA5);
        CHECK(ParseFrame(BuildFrame(1, Receiver::Camera, 1, payload.data(), payload.size())).valid);
        payload.push_back(0);
        CHECK(!ParseFrame(BuildFrame(1, Receiver::Camera, 1, payload.data(), payload.size())).valid);
        CHECK(!ParseFrame(BuildFrame(1, Receiver::Camera, 1, nullptr, 1)).valid);
        // All captured command bytes continue to pass the stricter parser.
        for (auto f : {SleepWakeFrame(0xAB, true), RecentreFrame(1), TrackingSpeedFrame(2, true)})
            CHECK(ParseFrame(f).valid);
    }

    // Simple tag commands.
    CheckPrefix(AiModeCommand(AiMode::Off), "16 02 00 00", "AI off");
    CheckPrefix(AiModeCommand(AiMode::UpperBody), "16 02 02 01", "AI upper body");
    CheckPrefix(AiModeCommand(AiMode::Group), "16 02 01 00", "AI group");
    CheckPrefix(AiModeCommand(AiMode::Desk), "16 02 05 00", "AI desk");
    CheckPrefix(HdrCommand(true), "01 01 01", "HDR on");
    CheckPrefix(FovCommand(Fov::Wide, Variant::Tiny2), "04 01 00", "FOV wide tiny2");
    CheckPrefix(FovCommand(Fov::Narrow, Variant::Lite), "04 01 03", "FOV narrow lite");

    // AI mode byte mapping round trip.
    for (int i = 0; i < static_cast<int>(AiMode::Count); ++i) {
        XuBuffer b = AiModeCommand(static_cast<AiMode>(i));
        bool known = false;
        CHECK(AiModeFromBytes(b[2], b[3], &known) == static_cast<AiMode>(i));
        CHECK(known);
    }

    // Status parsing.
    {
        XuBuffer s{};
        CHECK(!ParseStatus(s).valid);
        s[0x02] = 1; s[0x06] = 1; s[0x04] = 50; s[0x11] = 2; s[0x18] = 2; s[0x1C] = 1; s[0x24] = 2;
        Status st = ParseStatus(s);
        CHECK(st.valid && st.asleep && st.hdr && st.zoomPercent == 50);
        CHECK(st.aiMode == AiMode::UpperBody && st.aiModeKnown && st.sportTracking);
        Fov f;
        CHECK(FovFromStatus(st.fovRaw, Variant::Tiny2, &f) && f == Fov::Narrow);
        CHECK(FovFromStatus(st.fovRaw, Variant::Lite, &f) && f == Fov::Medium);
    }

    // Hex parsing.
    {
        std::vector<uint8_t> v;
        CHECK(ParseHex("0xAA, 25:0c-1 ff", &v));
        CHECK(v.size() == 5 && v[0] == 0xAA && v[3] == 0x01 && v[4] == 0xFF);
        CHECK(!ParseHex("zz", &v));
    }

    if (g_failures == 0) std::printf("All protocol tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
