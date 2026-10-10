// PreviewFormat: one of the camera's native capture formats, plus the helpers
// behind the preview's resolution / pixel format / frame rate pickers.
// Portable (no Windows headers) so it can be unit-tested on Linux.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

struct PreviewFormat {
    uint32_t width = 0, height = 0, fpsNum = 0, fpsDen = 1;
    std::string fourcc;  // e.g. "NV12", "MJPG"

    double Fps() const { return fpsDen ? static_cast<double>(fpsNum) / fpsDen : 0.0; }
    int RoundedFps() const { return static_cast<int>(std::lround(Fps())); }
    // Exact identity, used to ask for this format again.
    std::string Key() const {
        char buf[64];
        snprintf(buf, sizeof buf, "%ux%u:%s:%u/%u", width, height, fourcc.c_str(), fpsNum, fpsDen);
        return buf;
    }
    // e.g. "1280×720 NV12 30 fps"
    std::string Label() const { return SizeLabel(width, height) + " " + fourcc + " " + FpsLabel(RoundedFps()); }

    static std::string SizeLabel(uint32_t w, uint32_t h) {
        char buf[32];
        snprintf(buf, sizeof buf, "%u\xC3\x97%u", w, h);
        return buf;
    }
    static std::string FpsLabel(int fps) { return std::to_string(fps) + " fps"; }
};

namespace preview_format {

using Size = std::pair<uint32_t, uint32_t>;  // width, height

inline const PreviewFormat* Find(const std::vector<PreviewFormat>& list, const std::string& key) {
    for (const auto& f : list)
        if (f.Key() == key) return &f;
    return nullptr;
}

// Every frame size offered, largest first.
inline std::vector<Size> Resolutions(const std::vector<PreviewFormat>& list) {
    std::vector<Size> out;
    for (const auto& f : list)
        if (std::find(out.begin(), out.end(), Size{f.width, f.height}) == out.end()) out.push_back({f.width, f.height});
    std::stable_sort(out.begin(), out.end(), [](const Size& a, const Size& b) {
        const uint64_t pa = uint64_t{a.first} * a.second, pb = uint64_t{b.first} * b.second;
        return pa != pb ? pa > pb : a.first > b.first;
    });
    return out;
}

// The pixel formats offered at one frame size, in alphabetical order.
inline std::vector<std::string> PixelFormats(const std::vector<PreviewFormat>& list, uint32_t w, uint32_t h) {
    std::vector<std::string> out;
    for (const auto& f : list)
        if (f.width == w && f.height == h && std::find(out.begin(), out.end(), f.fourcc) == out.end())
            out.push_back(f.fourcc);
    std::sort(out.begin(), out.end());
    return out;
}

// The whole-number frame rates offered for one size and pixel format, fastest first.
inline std::vector<int> FrameRates(const std::vector<PreviewFormat>& list, uint32_t w, uint32_t h,
                                   const std::string& fourcc) {
    std::vector<int> out;
    for (const auto& f : list)
        if (f.width == w && f.height == h && f.fourcc == fourcc &&
            std::find(out.begin(), out.end(), f.RoundedFps()) == out.end())
            out.push_back(f.RoundedFps());
    std::sort(out.begin(), out.end(), [](int a, int b) { return a > b; });
    return out;
}

// The key of the closest format at size w×h: the wanted pixel format if it's
// offered there (else the fallback, else the first), then the nearest frame rate
// (ties go to the faster one). Empty when nothing is offered at that size.
inline std::string Choose(const std::vector<PreviewFormat>& list, uint32_t w, uint32_t h, const std::string& fourcc,
                          int fps, const std::string& fallbackFourcc = {}) {
    const std::vector<std::string> pixels = PixelFormats(list, w, h);
    if (pixels.empty()) return {};
    auto offered = [&](const std::string& p) { return std::find(pixels.begin(), pixels.end(), p) != pixels.end(); };
    const std::string pixel = offered(fourcc) ? fourcc : offered(fallbackFourcc) ? fallbackFourcc : pixels.front();

    const PreviewFormat* best = nullptr;
    for (const auto& f : list) {
        if (f.width != w || f.height != h || f.fourcc != pixel) continue;
        if (!best) { best = &f; continue; }
        const int d = std::abs(f.RoundedFps() - fps), bd = std::abs(best->RoundedFps() - fps);
        if (d < bd || (d == bd && f.Fps() > best->Fps())) best = &f;
    }
    return best ? best->Key() : std::string();
}

}  // namespace preview_format
