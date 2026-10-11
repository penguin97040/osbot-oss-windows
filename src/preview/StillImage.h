// StillImage: a small, softened copy of a preview frame, shown dimmed behind
// "Preview paused". Drawn at full size, the GPU's smooth scaling finishes the blur.
// Portable (no Windows headers) so it can be unit-tested on Linux.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace still_image {

// Averages a BGRA frame (w×h, tightly packed) down to about targetW pixels wide,
// keeping its shape, then softens it with two 3×3 box blurs. False if there's no image.
inline bool BlurredThumbnail(const uint8_t* bgra, int w, int h, int targetW, std::vector<uint8_t>* out, int* outW,
                             int* outH) {
    if (!bgra || !out || w <= 0 || h <= 0 || targetW <= 0) return false;
    const int tw = std::min(targetW, w);
    const int th = std::clamp(static_cast<int>(std::lround(static_cast<double>(h) * tw / w)), 1, h);
    std::vector<uint8_t> img(static_cast<size_t>(tw) * th * 4);
    for (int ty = 0; ty < th; ++ty) {
        const int64_t y0 = int64_t{ty} * h / th, y1 = std::max(y0 + 1, int64_t{ty + 1} * h / th);
        for (int tx = 0; tx < tw; ++tx) {
            const int64_t x0 = int64_t{tx} * w / tw, x1 = std::max(x0 + 1, int64_t{tx + 1} * w / tw);
            uint64_t sum[3] = {};
            for (int64_t y = y0; y < y1; ++y) {
                const uint8_t* row = bgra + (static_cast<size_t>(y) * w + static_cast<size_t>(x0)) * 4;
                for (int64_t x = x0; x < x1; ++x, row += 4)
                    for (int c = 0; c < 3; ++c) sum[c] += row[c];
            }
            const uint64_t n = static_cast<uint64_t>((y1 - y0) * (x1 - x0));
            uint8_t* px = &img[(static_cast<size_t>(ty) * tw + tx) * 4];
            for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(sum[c] / n);
            px[3] = 0xFF;
        }
    }
    std::vector<uint8_t> tmp(img.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                unsigned sum[3] = {}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= tw || sy >= th) continue;
                        const uint8_t* p = &img[(static_cast<size_t>(sy) * tw + sx) * 4];
                        for (int c = 0; c < 3; ++c) sum[c] += p[c];
                        ++n;
                    }
                uint8_t* q = &tmp[(static_cast<size_t>(y) * tw + x) * 4];
                for (int c = 0; c < 3; ++c) q[c] = static_cast<uint8_t>(sum[c] / n);
                q[3] = 0xFF;
            }
        img.swap(tmp);
    }
    *out = std::move(img);
    *outW = tw;
    *outH = th;
    return true;
}

}  // namespace still_image
