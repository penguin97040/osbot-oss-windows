#include "../src/preview/StillImage.h"

#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static std::vector<uint8_t> Frame(int w, int h, uint8_t b, uint8_t g, uint8_t r) {
    std::vector<uint8_t> f(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < f.size(); i += 4) {
        f[i] = b;
        f[i + 1] = g;
        f[i + 2] = r;
        f[i + 3] = 0;  // RGB32 alpha is undefined; the still must be opaque anyway
    }
    return f;
}

int main() {
    using still_image::BlurredThumbnail;
    std::vector<uint8_t> out;
    int w = 0, h = 0;

    {  // 16:9 keeps its shape; a plain colour stays exactly that colour and opaque.
        const auto f = Frame(1920, 1080, 10, 120, 250);
        CHECK(BlurredThumbnail(f.data(), 1920, 1080, 48, &out, &w, &h));
        CHECK(w == 48 && h == 27);
        CHECK(out.size() == static_cast<size_t>(w) * h * 4);
        bool plain = true;
        for (size_t i = 0; i < out.size(); i += 4)
            plain = plain && out[i] == 10 && out[i + 1] == 120 && out[i + 2] == 250 && out[i + 3] == 0xFF;
        CHECK(plain);
    }
    {  // One bright block spreads to its neighbours and gets dimmer.
        auto f = Frame(480, 480, 0, 0, 0);
        for (int y = 230; y < 250; ++y)
            for (int x = 230; x < 250; ++x) f[(static_cast<size_t>(y) * 480 + x) * 4 + 1] = 255;
        CHECK(BlurredThumbnail(f.data(), 480, 480, 48, &out, &w, &h));
        CHECK(w == 48 && h == 48);
        auto green = [&](int x, int y) { return out[(static_cast<size_t>(y) * w + x) * 4 + 1]; };
        CHECK(green(23, 23) > 0 && green(23, 23) < 255);
        CHECK(green(21, 23) > 0);    // spread sideways
        CHECK(green(0, 0) == 0);     // but not everywhere
    }
    {  // Smaller than the target, odd and tiny sizes: no overrun, never empty.
        const auto f = Frame(7, 3, 1, 2, 3);
        CHECK(BlurredThumbnail(f.data(), 7, 3, 48, &out, &w, &h));
        CHECK(w == 7 && h == 3);
        const auto one = Frame(1, 1, 9, 9, 9);
        CHECK(BlurredThumbnail(one.data(), 1, 1, 48, &out, &w, &h));
        CHECK(w == 1 && h == 1 && out[0] == 9);
        const auto tall = Frame(3, 2000, 5, 5, 5);
        CHECK(BlurredThumbnail(tall.data(), 3, 2000, 48, &out, &w, &h));
        CHECK(w == 3 && h == 2000);
    }
    CHECK(!BlurredThumbnail(nullptr, 10, 10, 48, &out, &w, &h));
    CHECK(!BlurredThumbnail(out.data(), 0, 10, 48, &out, &w, &h));

    if (failures) std::printf("%d check(s) failed\n", failures);
    else std::printf("All still image tests passed.\n");
    return failures ? 1 : 0;
}
