#include "Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>

namespace {

std::wstring FilePath() { return Settings::Directory() + L"\\settings.ini"; }

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
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData))) {
        dir = appData;
        CoTaskMemFree(appData);
    } else {
        dir = L".";
    }
    dir += L"\\osbot-oss-windows";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void Settings::Load() {
    FILE* f = _wfopen(FilePath().c_str(), L"rb");
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

    auto getInt = [&](const char* k, int def) { auto it = kv.find(k); return it == kv.end() ? def : atoi(it->second.c_str()); };
    auto getLong = [&](const std::string& k, long def) { auto it = kv.find(k); return it == kv.end() ? def : atol(it->second.c_str()); };
    auto getFloat = [&](const char* k, float def) { auto it = kv.find(k); return it == kv.end() ? def : static_cast<float>(atof(it->second.c_str())); };

    previewEnabled = getInt("preview", previewEnabled) != 0;
    antiFlicker = getInt("anti_flicker", antiFlicker);
    variant = getInt("protocol_variant", variant);
    moveMethod = getInt("move_method", moveMethod);
    moveSpeed = getFloat("move_speed", moveSpeed);
    invertPan = getInt("invert_pan", invertPan) != 0;
    invertTilt = getInt("invert_tilt", invertTilt) != 0;
    showDeveloper = getInt("show_developer", showDeveloper) != 0;
    windowW = getInt("window_w", windowW);
    windowH = getInt("window_h", windowH);
    if (auto it = kv.find("device_path"); it != kv.end()) devicePath = FromUtf8(it->second);
    for (size_t i = 0; i < presets.size(); ++i) {
        const std::string p = "preset" + std::to_string(i + 1) + "_";
        presets[i].saved = getLong(p + "saved", 0) != 0;
        presets[i].pan = getLong(p + "pan", 0);
        presets[i].tilt = getLong(p + "tilt", 0);
        presets[i].zoom = getLong(p + "zoom", 0);
    }
}

void Settings::Save() const {
    const std::wstring path = FilePath();
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
    fclose(f);
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
