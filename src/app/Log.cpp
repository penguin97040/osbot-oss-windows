#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace logx {

namespace {
constexpr size_t kMaxEntries = 2000;
constexpr long kMaxFileBytes = 10L * 1024 * 1024;  // stop mirroring to disk past this
std::mutex g_mutex;
std::deque<Entry> g_entries;
FILE* g_file = nullptr;

const char* LevelTag(Level l) {
    switch (l) {
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    default: return "INFO ";
    }
}
}  // namespace

void OpenFile(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) fclose(g_file);
    g_file = _wfopen(path.c_str(), L"wb");
}

void Write(Level level, const char* fmt, ...) {
    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char time[16];
    snprintf(time, sizeof time, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.push_back({time, level, text});
    if (g_entries.size() > kMaxEntries) g_entries.pop_front();
    if (g_file) {
        fprintf(g_file, "%s %s %s\r\n", time, LevelTag(level), text);
        if (ftell(g_file) > kMaxFileBytes) {
            fprintf(g_file, "%s %s Log file size limit reached; later entries are only shown in the app.\r\n",
                    time, LevelTag(Level::Warn));
            fclose(g_file);
            g_file = nullptr;
        } else {
            fflush(g_file);
        }
    }
}

std::vector<Entry> Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<Entry>(g_entries.begin(), g_entries.end());
}

std::string AllAsText() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string out;
    for (const auto& e : g_entries) {
        out += e.time;
        out += ' ';
        out += LevelTag(e.level);
        out += ' ';
        out += e.text;
        out += "\r\n";
    }
    return out;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
}

std::string HrText(long hr) {
    char buf[512];
    int n = snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    char* msg = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_NZ),
                               reinterpret_cast<char*>(&msg), 0, nullptr);
    if (len == 0) {
        len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    }
    if (len && msg) {
        while (len && (msg[len - 1] == '\r' || msg[len - 1] == '\n' || msg[len - 1] == ' ')) msg[--len] = 0;
        snprintf(buf + n, sizeof buf - n, " (%s)", msg);
    }
    if (msg) LocalFree(msg);
    return buf;
}

}  // namespace logx
