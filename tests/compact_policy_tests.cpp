#include "../src/app/CompactPolicy.h"

#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main() {
    using compact::Policy;
    using compact::Switch;

    {  // Another app starts and stops using the camera: compact, then full again.
        Policy p;
        CHECK(p.OnDetected(false, false) == Switch::None);
        CHECK(p.OnDetected(true, false) == Switch::ToCompact);
        CHECK(p.OnDetected(true, true) == Switch::None);  // only changes count
        CHECK(p.OnDetected(false, true) == Switch::ToFull);
        CHECK(p.OnDetected(false, false) == Switch::None);
    }
    {  // Opened during a call: the first check goes compact.
        Policy p;
        CHECK(p.OnDetected(true, false) == Switch::ToCompact);
    }
    {  // Back to full view by hand during the call: stays full, and nothing happens when it ends.
        Policy p;
        CHECK(p.OnDetected(true, false) == Switch::ToCompact);
        p.OnManualToggle();
        CHECK(p.OnDetected(true, false) == Switch::None);
        CHECK(p.OnDetected(false, false) == Switch::None);
        // The next call switches again.
        CHECK(p.OnDetected(true, false) == Switch::ToCompact);
    }
    {  // Compact chosen by hand before a call: still compact after it.
        Policy p;
        p.OnManualToggle();
        CHECK(p.OnDetected(true, true) == Switch::None);
        CHECK(p.OnDetected(false, true) == Switch::None);
    }
    {  // Toggled to full and back to compact during the call: the user's choice, so it stays.
        Policy p;
        CHECK(p.OnDetected(true, false) == Switch::ToCompact);
        p.OnManualToggle();
        p.OnManualToggle();
        CHECK(p.OnDetected(false, true) == Switch::None);
    }

    CHECK(compact::InUse(200, 0));
    CHECK(compact::InUse(200, 100));
    CHECK(!compact::InUse(100, 200));
    CHECK(!compact::InUse(0, 0));

    CHECK(compact::IsSamePath(L"C:#Tools#osbot-oss-windows.exe", L"c:\\tools\\OSBOT-OSS-WINDOWS.EXE"));
    CHECK(!compact::IsSamePath(L"C:#Tools#other.exe", L"C:\\Tools\\osbot.exe"));
    CHECK(!compact::IsSamePath(L"", L""));

    CHECK(compact::AppName(L"C:#Program Files#Zoom#bin#Zoom.exe", false) == L"Zoom");
    CHECK(compact::AppName(L"C:#Users#me#AppData#Local#Discord#app-1.0.9#Discord.EXE", false) == L"Discord");
    CHECK(compact::AppName(L"C:#Tools#obs64", false) == L"obs64");
    CHECK(compact::AppName(L"Microsoft.WindowsCamera_8wekyb3d8bbwe", true) == L"WindowsCamera");
    CHECK(compact::AppName(L"MSTeams_8wekyb3d8bbwe", true) == L"MSTeams");
    CHECK(compact::AppName(L"C:#", false) == L"C:#");
    CHECK(compact::ExeFileName(L"C:#Program Files#Zoom#bin#Zoom.exe") == L"Zoom.exe");
    CHECK(compact::ExeFileName(L"Zoom.exe") == L"Zoom.exe");
    CHECK(compact::ExeFileName(L"C:#").empty());

    if (failures) std::printf("%d check(s) failed\n", failures);
    else std::printf("All compact policy tests passed.\n");
    return failures ? 1 : 0;
}
