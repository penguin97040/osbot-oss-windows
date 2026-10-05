// Thread-safe in-memory activity log, mirrored to a local log file.
#pragma once

#include <string>
#include <vector>

namespace logx {

enum class Level { Info, Warn, Error };

struct Entry {
    std::string time;  // HH:MM:SS (24-hour, local time)
    Level level;
    std::string text;
};

// Optional: mirror every entry to this UTF-8 file (truncated on open).
void OpenFile(const std::wstring& path);

void Write(Level level, const char* fmt, ...);
#define LOG_INFO(...) ::logx::Write(::logx::Level::Info, __VA_ARGS__)
#define LOG_WARN(...) ::logx::Write(::logx::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::logx::Write(::logx::Level::Error, __VA_ARGS__)

// Copies all current entries (at most a few thousand).
std::vector<Entry> Snapshot();
std::string AllAsText();
void Clear();

// Formats a Windows HRESULT as "0x80070005 (Access is denied.)".
std::string HrText(long hr);

}  // namespace logx
