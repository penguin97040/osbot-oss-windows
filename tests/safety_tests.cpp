#include "../src/Safety.h"
#include "../src/preview/AsyncMailbox.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <limits>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main() {
    using namespace safety;
    constexpr auto min = std::numeric_limits<int32_t>::min();
    constexpr auto max = std::numeric_limits<int32_t>::max();
    CHECK(ParseNumber<int32_t>("2147483647", 7) == max);
    CHECK(ParseNumber<int32_t>("-2147483648", 7) == min);
    for (const char* text : {"", "2147483648", "-2147483649", "999999999999999999999",
                             "12x", "1.5", "1 ", " 1", "--1"})
        CHECK(ParseNumber<int32_t>(text, 7) == 7);
    for (const char* text : {"", "nan", "inf", "-inf", "1e1000", "1e-1000", "20junk"})
        CHECK(ParseNumber<float>(text, 30) == 30);
    CHECK(ParseNumber<float>("25.5", 30) == 25.5f);
    CHECK(MoveSpeed(-100) == 5 && MoveSpeed(1000) == 90);
    CHECK(MoveSpeed(std::numeric_limits<float>::quiet_NaN()) == 30);
    CHECK(Velocity(std::numeric_limits<float>::infinity()) == 0);
    CHECK(Velocity(1) == 5 && Velocity(-1) == -5);
    CHECK(Velocity(-1000) == -90 && Velocity(1000) == 90 && Velocity(0) == 0);
    CHECK(AngleValue(static_cast<double>(max) + 100, 1, min, max) == max);
    CHECK(AngleValue(static_cast<double>(min) - 100, 1, min, max) == min);
    CHECK(AngleValue(1e300, 3600, min, max) == max);
    CHECK(AngleValue(-1e300, 3600, min, max) == min);
    CHECK(AngleValue(12.5, 3600, min, max) == 45000);
    CHECK(StepValue(max, min, max, max, 1) == max);
    CHECK(StepValue(min, min, max, max, -1) == min);
    CHECK(StepValue(0, min, max, 1, 1) == 107374182);

    CHECK(Geometry(4096, 4096));
    CHECK(!Geometry(4096, 4097) && !Geometry(0, 1) && !Geometry(16385, 1));
    CHECK(FrameBounds(2, 3, 12, 0, 32));
    CHECK(FrameBounds(2, 3, -12, 24, 32));
    CHECK(!FrameBounds(2, 3, 12, 0, 31));
    CHECK(!FrameBounds(2, 3, -12, 24, 31));
    CHECK(!FrameBounds(2, 3, -12, 23, 32));
    CHECK(!FrameBounds(2, 3, 7, 0, 32));
    CHECK(!FrameBounds(2, 3, min, 0, 32));
    CHECK(!FrameBounds(2, 3, min, uint64_t{1} << 32, uint64_t{1} << 33));
    CHECK(!FrameBounds(1, 1, 4, 9, 8));
    CHECK(!FrameBounds(1, 1, 4, 0, kPreviewLimit + 1));
    // Independently enumerate the row bytes to check the bounds predicate.
    for (int32_t pitch : {-16, -8, 0, 4, 8, 16}) {
        for (uint64_t offset = 0; offset < 40; ++offset) {
            for (uint64_t length = 0; length < 40; ++length) {
                bool fits = (pitch <= -8 || pitch >= 8);
                for (int y = 0; y < 3; ++y) {
                    const int64_t row = static_cast<int64_t>(offset) + int64_t{pitch} * y;
                    fits = fits && row >= 0 && row + 8 <= static_cast<int64_t>(length);
                }
                CHECK(FrameBounds(2, 3, pitch, offset, length) == fits);
            }
        }
    }

    GimbalMotion gimbal{true};
    std::vector<int> calls;
    CHECK(!gimbal.Close([&] { calls.push_back(1); return false; }, [&] { calls.push_back(2); }));
    CHECK(gimbal.moving && calls == std::vector<int>({1, 1}));
    CHECK(gimbal.Close([&] { calls.push_back(1); return true; }, [&] { calls.push_back(2); }));
    CHECK(!gimbal.moving && calls == std::vector<int>({1, 1, 1, 1, 2}));
    gimbal.moving = true;
    int stopAttempts = 0;
    CHECK(gimbal.Stop([&] { return ++stopAttempts == 2; }));
    CHECK(!gimbal.moving && stopAttempts == 2);

    UploadProgress upload;
    int attempts = 0;
    // Texture creation, view creation and mapping failures must all retry the same frame.
    for (int failure = 0; failure < 3; ++failure) {
        CHECK(!upload.Try(1, [&] { ++attempts; return false; }));
        CHECK(upload.uploaded == 0);
    }
    CHECK(upload.Try(1, [&] { ++attempts; return true; }));
    CHECK(upload.uploaded == 1 && attempts == 4);
    CHECK(upload.Try(1, [&] { ++attempts; return false; }));
    CHECK(attempts == 4);

    auto mailbox = std::make_shared<AsyncMailbox<std::shared_ptr<int>>>();
    CHECK(mailbox->BeginRead());
    CHECK(!mailbox->BeginRead());
    auto result = std::make_shared<int>(42);
    mailbox->Deliver(result);
    CHECK(!mailbox->BeginRead());
    std::shared_ptr<int> received;
    CHECK(mailbox->Wait(received) && *received == 42);
    received.reset();
    CHECK(mailbox->BeginRead());
    auto waiter = std::async(std::launch::async, [mailbox] {
        std::shared_ptr<int> value;
        return mailbox->Wait(value);
    });
    // Cancellation wakes the wait even when no callback/sample ever arrives.
    mailbox->Cancel();
    CHECK(waiter.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(!waiter.get());
    CHECK(!mailbox->BeginRead());
    bool published = false;
    mailbox->IfActive([&] { published = true; });
    CHECK(!published);
    // A callback can outlive the worker/preview; cancelled results aren't retained.
    std::weak_ptr<int> weakResult = result;
    auto lateCallback = mailbox;
    mailbox.reset();
    lateCallback->Deliver(std::move(result));
    CHECK(weakResult.expired());
    for (int i = 0; i < 100; ++i) {
        AsyncMailbox<int> race;
        CHECK(race.BeginRead());
        auto callback = std::async(std::launch::async, [&] { race.Deliver(42); });
        race.Cancel();
        callback.get();
        int value = 0;
        CHECK(!race.Wait(value));
        race.IfActive([&] { ++failures; });
    }

    if (!failures) std::printf("All safety tests passed.\n");
    return failures ? 1 : 0;
}
