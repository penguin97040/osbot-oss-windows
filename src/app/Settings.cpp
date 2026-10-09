#include "Settings.h"
#include "../Safety.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>

namespace {

std::wstring FilePath() {
    const auto dir = Settings::Directory();
    return dir.empty() ? std::wstring{} : dir + L"\\settings.ini";
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

}  // namespace

std::wstring Settings::Directory() {
    static const std::wstring directory = []() -> std::wstring {
        PWSTR appData = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData);
        std::wstring dir;
        if (SUCCEEDED(hr) && appData) dir = appData;
        CoTaskMemFree(appData);
        if (dir.empty()) return {};
        dir += L"\\osbot-oss-windows";
        if (!CreateDirectoryW(dir.c_str(), nullptr)) {
            if (GetLastError() != ERROR_ALREADY_EXISTS) return {};
            const DWORD attrs = GetFileAttributesW(dir.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
        }
        return dir;
    }();
    return directory;
}

void Settings::Load() {
    const auto path = FilePath();
    if (path.empty()) return;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::map<std::string, std::string> kv;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        kv[s.substr(0, eq)] = s.substr(eq + 1);
    }
    fclose(f);

    auto getInt = [&](const char* k, int def) { auto it = kv.find(k); return it == kv.end() ? def : safety::ParseNumber(it->second, def); };
    auto getLong = [&](const std::string& k, long def) { auto it = kv.find(k); return it == kv.end() ? def : safety::ParseNumber(it->second, def); };
    auto getFloat = [&](const char* k, float def) { auto it = kv.find(k); return it == kv.end() ? def : safety::ParseNumber(it->second, def); };

    auto getBool = [&](const char* k, bool def) {
        const int value = getInt(k, def ? 1 : 0);
        return value == 0 ? false : value == 1 ? true : def;
    };
    previewEnabled = getBool("preview", previewEnabled);
    antiFlicker = getInt("anti_flicker", antiFlicker);
    variant = getInt("protocol_variant", variant);
    moveMethod = getInt("move_method", moveMethod);
    moveSpeed = getFloat("move_speed", moveSpeed);
    invertPan = getBool("invert_pan", invertPan);
    invertTilt = getBool("invert_tilt", invertTilt);
    showDeveloper = getBool("show_developer", showDeveloper);
    // A hand-edited or damaged file must not produce out-of-range values (the
    // move speed goes straight to the gimbal). Fall back to the defaults.
    const Settings defaults;
    if (antiFlicker < -1 || antiFlicker > 2) antiFlicker = defaults.antiFlicker;
    if (variant < 0 || variant > 2) variant = defaults.variant;
    if (moveMethod < 0 || moveMethod > 1) moveMethod = defaults.moveMethod;
    if (!std::isfinite(moveSpeed)) moveSpeed = defaults.moveSpeed;
    moveSpeed = std::clamp(moveSpeed, 5.0f, 90.0f);  // same range as the speed slider
    windowW = getInt("window_w", windowW);
    windowH = getInt("window_h", windowH);
    if (auto it = kv.find("device_path"); it != kv.end()) devicePath = FromUtf8(it->second);
    for (size_t i = 0; i < presets.size(); ++i) {
        const std::string p = "preset" + std::to_string(i + 1) + "_";
        presets[i].saved = getLong(p + "saved", 0) == 1;
        presets[i].pan = getLong(p + "pan", 0);
        presets[i].tilt = getLong(p + "tilt", 0);
        presets[i].zoom = getLong(p + "zoom", 0);
    }
}

void Settings::Save() const {
    const std::wstring path = FilePath();
    if (path.empty()) return;
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    fprintf(f, "# osbot-oss-windows settings. Safe to delete; defaults will be used.\r\n");
    fprintf(f, "preview=%d\r\n", previewEnabled ? 1 : 0);
    fprintf(f, "anti_flicker=%d\r\n", antiFlicker);
    fprintf(f, "protocol_variant=%d\r\n", variant);
    fprintf(f, "move_method=%d\r\n", moveMethod);
    fprintf(f, "move_speed=%.1f\r\n", moveSpeed);
    fprintf(f, "invert_pan=%d\r\n", invertPan ? 1 : 0);
    fprintf(f, "invert_tilt=%d\r\n", invertTilt ? 1 : 0);
    fprintf(f, "show_developer=%d\r\n", showDeveloper ? 1 : 0);
    fprintf(f, "window_w=%d\r\n", windowW);
    fprintf(f, "window_h=%d\r\n", windowH);
    fprintf(f, "device_path=%s\r\n", ToUtf8(devicePath).c_str());
    for (size_t i = 0; i < presets.size(); ++i) {
        fprintf(f, "preset%zu_saved=%d\r\n", i + 1, presets[i].saved ? 1 : 0);
        fprintf(f, "preset%zu_pan=%ld\r\n", i + 1, presets[i].pan);
        fprintf(f, "preset%zu_tilt=%ld\r\n", i + 1, presets[i].tilt);
        fprintf(f, "preset%zu_zoom=%ld\r\n", i + 1, presets[i].zoom);
    }
    // Only replace the old file if the new one was written completely (e.g. not on a full disk).
    const bool ok = !ferror(f);
    if (fclose(f) == 0 && ok)
        MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else
        DeleteFileW(tmp.c_str());
}
