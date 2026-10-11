// CompactPolicy: when the window switches itself between full and compact view,
// and how Windows' camera-use records are read. Portable (no Windows headers) so
// it can be unit-tested on Linux.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>

namespace compact {

enum class Switch { None, ToCompact, ToFull };

// Reacts only when another app starts or stops using the camera, so a choice made
// with the Compact button stays until the next change.
class Policy {
public:
    Switch OnDetected(bool inUse, bool compactNow) {
        if (inUse == inUse_) return Switch::None;
        inUse_ = inUse;
        if (inUse) {
            if (compactNow) return Switch::None;  // already compact by choice: keep it after the call
            automatic_ = true;
            return Switch::ToCompact;
        }
        const bool wasAutomatic = automatic_;
        automatic_ = false;
        return wasAutomatic && compactNow ? Switch::ToFull : Switch::None;
    }
    void OnManualToggle() { automatic_ = false; }

private:
    bool inUse_ = false;
    bool automatic_ = false;  // compact because of the camera, not the button
};

// Windows sets LastUsedTimeStart when an app opens a camera and LastUsedTimeStop
// (0 until then) when it lets go.
inline bool InUse(uint64_t start, uint64_t stop) { return start != 0 && start > stop; }

// A desktop app's key is its exe path with '\' written as '#'.
inline bool IsSamePath(const std::wstring& key, const std::wstring& exePath) {
    if (key.size() != exePath.size() || key.empty()) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        const wchar_t a = key[i], b = exePath[i] == L'\\' ? L'#' : exePath[i];
        if (std::towlower(static_cast<wint_t>(a)) != std::towlower(static_cast<wint_t>(b))) return false;
    }
    return true;
}

// A readable name from the key: "C:#Program Files#Zoom#bin#Zoom.exe" -> "Zoom",
// "Microsoft.WindowsCamera_8wekyb3d8bbwe" (a Store app) -> "WindowsCamera".
inline std::wstring AppName(const std::wstring& key, bool packaged) {
    std::wstring name = key;
    if (packaged) {
        name.resize(std::min(name.size(), name.find(L'_')));
        const size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < name.size()) name.erase(0, dot + 1);
    } else {
        const size_t hash = name.rfind(L'#');
        if (hash != std::wstring::npos) name.erase(0, hash + 1);
        if (name.size() > 4) {
            std::wstring ext = name.substr(name.size() - 4);
            for (auto& ch : ext) ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
            if (ext == L".exe") name.resize(name.size() - 4);
        }
    }
    return name.empty() ? key : name;
}

}  // namespace compact
