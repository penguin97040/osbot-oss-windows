#include "CameraUse.h"
#include "CompactPolicy.h"

#include <windows.h>

#include <cstdint>

namespace {

constexpr wchar_t kWebcamKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam";

uint64_t ReadTime(HKEY key, const wchar_t* name) {
    uint64_t value = 0;
    DWORD type = 0, size = sizeof value;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_QWORD || size != sizeof value)
        return 0;
    return value;
}

// Looks through the app keys under parent; on a match fills appName and returns true.
bool FindUser(HKEY parent, bool packaged, const std::wstring& self, std::wstring* appName) {
    wchar_t name[256];  // registry key names are at most 255 characters
    for (DWORD i = 0; i < 4096; ++i) {
        DWORD len = ARRAYSIZE(name);
        const LONG r = RegEnumKeyExW(parent, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        const std::wstring key(name, len);
        if (packaged ? key == L"NonPackaged" : compact::IsSamePath(key, self)) continue;
        HKEY sub = nullptr;
        if (RegOpenKeyExW(parent, name, 0, KEY_QUERY_VALUE, &sub) != ERROR_SUCCESS) continue;
        const bool inUse = compact::InUse(ReadTime(sub, L"LastUsedTimeStart"), ReadTime(sub, L"LastUsedTimeStop"));
        RegCloseKey(sub);
        if (inUse) {
            *appName = compact::AppName(key, packaged);
            return true;
        }
    }
    return false;
}

std::string ToUtf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

CameraUse QueryCameraUse() {
    wchar_t path[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    const std::wstring self = n > 0 && n < ARRAYSIZE(path) ? std::wstring(path, n) : std::wstring();

    std::wstring appName;
    bool found = false;
    for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        HKEY webcam = nullptr;
        if (RegOpenKeyExW(hive, kWebcamKey, 0, KEY_READ, &webcam) != ERROR_SUCCESS) continue;
        HKEY desktop = nullptr;
        if (RegOpenKeyExW(webcam, L"NonPackaged", 0, KEY_READ, &desktop) == ERROR_SUCCESS) {
            found = FindUser(desktop, false, self, &appName);
            RegCloseKey(desktop);
        }
        if (!found) found = FindUser(webcam, true, self, &appName);
        RegCloseKey(webcam);
        if (found) break;
    }
    CameraUse use;
    use.inUse = found;
    if (found) use.appName = ToUtf8(appName);
    return use;
}
