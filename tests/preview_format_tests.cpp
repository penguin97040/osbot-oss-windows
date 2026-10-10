#include "../src/preview/PreviewFormat.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static PreviewFormat F(uint32_t w, uint32_t h, const char* fourcc, uint32_t num, uint32_t den = 1) {
    PreviewFormat f;
    f.width = w;
    f.height = h;
    f.fourcc = fourcc;
    f.fpsNum = num;
    f.fpsDen = den;
    return f;
}

int main() {
    using namespace preview_format;
    const std::vector<PreviewFormat> list = {
        F(1280, 720, "NV12", 30),        F(1280, 720, "NV12", 30000, 1001), F(1280, 720, "MJPG", 60),
        F(1280, 720, "MJPG", 30),        F(3840, 2160, "MJPG", 30),         F(1920, 1080, "NV12", 30),
        F(1920, 1080, "MJPG", 60),       F(1920, 1080, "YUY2", 5),          F(640, 360, "YUY2", 30),
        F(1280, 720, "YUY2", 10),
    };

    CHECK(PreviewFormat::SizeLabel(1280, 720) == "1280\xC3\x97" "720");
    CHECK(F(1280, 720, "NV12", 30000, 1001).Label() == "1280\xC3\x97" "720 NV12 30 fps");
    CHECK(F(1, 1, "NV12", 1, 0).Fps() == 0.0);

    // Sizes: unique, largest first.
    const std::vector<Size> sizes = Resolutions(list);
    CHECK(sizes.size() == 4);
    CHECK(sizes.front() == Size(3840, 2160) && sizes.back() == Size(640, 360));
    CHECK(sizes[1] == Size(1920, 1080) && sizes[2] == Size(1280, 720));
    CHECK(Resolutions({}).empty());

    // Pixel formats at one size: unique and in a stable order.
    CHECK((PixelFormats(list, 1280, 720) == std::vector<std::string>{"MJPG", "NV12", "YUY2"}));
    CHECK((PixelFormats(list, 3840, 2160) == std::vector<std::string>{"MJPG"}));
    CHECK(PixelFormats(list, 800, 600).empty());

    // Frame rates: 30 and 29.97 fps show once; fastest first.
    CHECK((FrameRates(list, 1280, 720, "NV12") == std::vector<int>{30}));
    CHECK((FrameRates(list, 1280, 720, "MJPG") == std::vector<int>{60, 30}));
    CHECK(FrameRates(list, 1280, 720, "H264").empty());

    // Exact choice; 30 beats 29.97 for "30 fps".
    CHECK(Choose(list, 1280, 720, "NV12", 30) == "1280x720:NV12:30/1");
    CHECK(Choose(list, 1280, 720, "MJPG", 60) == "1280x720:MJPG:60/1");
    // Changing size keeps the pixel format and frame rate when offered...
    CHECK(Choose(list, 1920, 1080, "NV12", 30) == "1920x1080:NV12:30/1");
    CHECK(Choose(list, 1920, 1080, "MJPG", 60) == "1920x1080:MJPG:60/1");
    // ...otherwise falls back to the default's format, then the first one offered.
    CHECK(Choose(list, 3840, 2160, "NV12", 30, "NV12") == "3840x2160:MJPG:30/1");
    CHECK(Choose(list, 640, 360, "NV12", 30, "MJPG") == "640x360:YUY2:30/1");
    CHECK(Choose(list, 1920, 1080, "YUY2", 30) == "1920x1080:YUY2:5/1");
    CHECK(Choose(list, 1920, 1080, "H264", 30, "NV12") == "1920x1080:NV12:30/1");
    // Nearest frame rate; ties go to the faster one.
    CHECK(Choose(list, 1280, 720, "MJPG", 50) == "1280x720:MJPG:60/1");
    CHECK(Choose(list, 1280, 720, "MJPG", 45) == "1280x720:MJPG:60/1");
    CHECK(Choose(list, 1280, 720, "MJPG", 10) == "1280x720:MJPG:30/1");
    // Nothing at that size.
    CHECK(Choose(list, 800, 600, "NV12", 30).empty());
    CHECK(Choose({}, 1280, 720, "NV12", 30).empty());

    CHECK(Find(list, "1920x1080:MJPG:60/1") == &list[6]);
    CHECK(Find(list, "nope") == nullptr);

    if (!failures) std::printf("All preview format tests passed.\n");
    return failures ? 1 : 0;
}
