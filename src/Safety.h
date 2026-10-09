// Bounds and numeric checks shared by the UI, workers and regression tests.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace safety {
constexpr uint64_t kPreviewLimit = 64ULL * 1024 * 1024;

template<class T> T ParseNumber(std::string_view text, T fallback) {
    if (text.empty()) return fallback;
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return fallback;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) return fallback;
    }
    return value;
}

inline float MoveSpeed(float speed) {
    return std::isfinite(speed) ? std::clamp(speed, 5.0f, 90.0f) : 30.0f;
}

inline float Velocity(float value) {
    if (!std::isfinite(value)) return 0;
    return value == 0 ? 0 : std::copysign(MoveSpeed(std::fabs(value)), value);
}

inline int32_t AngleValue(double degrees, double perDegree, int32_t min, int32_t max) {
    if (!std::isfinite(degrees) || !std::isfinite(perDegree) || min > max) return min;
    // Clamp in floating point before rounding or converting to a signed integer.
    const double value = std::clamp(degrees * perDegree, static_cast<double>(min), static_cast<double>(max));
    return static_cast<int32_t>(std::round(value));
}

inline int32_t StepValue(int32_t value, int32_t min, int32_t max, int32_t step, int direction) {
    const int64_t delta = std::max<int64_t>(step, (static_cast<int64_t>(max) - min) / 40);
    const int64_t target = static_cast<int64_t>(value) + direction * delta;
    return static_cast<int32_t>(std::clamp<int64_t>(target, min, max));
}

inline bool Geometry(uint32_t width, uint32_t height) {
    return width && height && width <= 16384 && height <= 16384 &&
           static_cast<uint64_t>(width) * height * 4 <= kPreviewLimit;
}

// scanOffset is the top row within the allocation. Check both ends before any
// pointer arithmetic, including bottom-up images and the minimum signed pitch.
inline bool FrameBounds(uint32_t width, uint32_t height, int32_t pitch,
                        uint64_t scanOffset, uint64_t allocation) {
    if (!Geometry(width, height) || allocation > kPreviewLimit || scanOffset > allocation) return false;
    const uint64_t row = static_cast<uint64_t>(width) * 4;
    const uint64_t stride = pitch < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(pitch))
                                      : static_cast<uint64_t>(pitch);
    if (stride < row) return false;
    const uint64_t span = stride * (height - 1);
    if (pitch < 0) return span <= scanOffset && row <= allocation - scanOffset;
    return span <= allocation - scanOffset && row <= allocation - scanOffset - span;
}

// Failed stops keep movement latched so the next service pass can retry.
struct GimbalMotion {
    bool moving = false;
    template<class Send, class CloseDevice> bool Close(Send send, CloseDevice close) {
        if (!Stop(send)) return false;
        close();
        return true;
    }
    template<class Send> bool Stop(Send send) {
        if (!moving) return true;
        const bool first = send();
        const bool second = send();
        if (first || second) moving = false;
        return !moving;
    }
};

// Commit an upload only when every graphics operation has succeeded.
struct UploadProgress {
    uint64_t uploaded = 0;
    template<class Upload> bool Try(uint64_t frame, Upload upload) {
        if (frame == uploaded) return true;
        if (!upload()) return false;
        uploaded = frame;
        return true;
    }
};
}  // namespace safety
