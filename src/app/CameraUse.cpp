#include "CameraUse.h"
#include "CompactPolicy.h"
#include "Utf8.h"

#include <windows.h>
#include <tlhelp32.h>

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

// This exe's path as Windows records it: the long form, in case it was started
// through a short (8.3) path. Worked out once.
const std::wstring& SelfPath() {
    static const std::wstring self = [] {
        wchar_t path[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (n == 0 || n >= ARRAYSIZE(path)) return std::wstring();
        wchar_t longPath[MAX_PATH * 2];
        const DWORD m = GetLongPathNameW(path, longPath, ARRAYSIZE(longPath));
        return m > 0 && m < ARRAYSIZE(longPath) ? std::wstring(longPath, m) : std::wstring(path, n);
    }();
    return self;
}

// Whether a process with this exe file name is running. Used to skip a record left
// behind by an app that crashed while using the camera (Windows never wrote its stop
// time). If the process list can't be read, the record is trusted.
bool IsRunning(const std::wstring& exeName) {
    if (exeName.empty()) return true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    bool running = false;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !running; ok = Process32NextW(snapshot, &entry))
        running = _wcsicmp(entry.szExeFile, exeName.c_str()) == 0;
    CloseHandle(snapshot);
    return running;
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
        if (inUse && (packaged || IsRunning(compact::ExeFileName(key)))) {
            *appName = compact::AppName(key, packaged);
            return true;
        }
    }
    return false;
}

}  // namespace

CameraUse QueryCameraUse() {
    const std::wstring& self = SelfPath();

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
