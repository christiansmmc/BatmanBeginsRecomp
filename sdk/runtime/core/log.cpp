#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace logx {

static std::mutex g_mutex;
static FILE* g_file = nullptr;
static std::string g_path;
static auto g_start = std::chrono::steady_clock::now();

constexpr int kKeep = 30;
constexpr int kKeepSessions = 5;  // a release build: this session and the four before it

// the run number of prefix_NNN.log, 0 for another name
static int RunNumber(const fs::path& file, const std::string& prefix) {
    std::string name = file.filename().string();
    if (file.extension() != ".log" || name.size() <= prefix.size() + 1 || name.compare(0, prefix.size() + 1, prefix + "_"))
        return 0;
    return std::atoi(name.c_str() + prefix.size() + 1);
}

// prefix.log becomes prefix.1.log and so on, oldest first, and the oldest goes. A file that cannot move stays where it is
static void MoveSessionsAlong(const fs::path& dir, const std::string& prefix) {
    auto file = [&](int session) {
        return dir / (session ? prefix + "." + std::to_string(session) + ".log" : prefix + ".log");
    };
    std::error_code ec;
    fs::remove(file(kKeepSessions - 1), ec);
    for (int session = kKeepSessions - 2; session >= 0; session--) fs::rename(file(session), file(session + 1), ec);
}

void Init(const std::string& dir, const std::string& prefix, bool append) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!kVerbose) {
        if (!append) MoveSessionsAlong(dir, prefix);
        g_path = (fs::path(dir) / (prefix + ".log")).string();
        g_file = std::fopen(g_path.c_str(), "a");  // never "w": a file that could not move still holds a session
        return;
    }
    std::vector<std::pair<int, fs::path>> runs;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (int n = RunNumber(e.path(), prefix)) runs.emplace_back(n, e.path());
    std::sort(runs.begin(), runs.end());
    for (size_t i = 0; i + kKeep <= runs.size(); i++) fs::remove(runs[i].second, ec);
    char name[256];
    std::snprintf(name, sizeof(name), "%s_%03d.log", prefix.c_str(), runs.empty() ? 1 : runs.back().first + 1);
    g_path = (fs::path(dir) / name).string();
    g_file = std::fopen(g_path.c_str(), "w");
}

const std::string& Path() { return g_path; }

void Write(Level lv, const char* fmt, ...) {
    // XBE_PERF asks for the perf lines, which a release build would otherwise drop
    static const bool all_levels = kVerbose || std::getenv("XBE_PERF");
    if (!all_levels && lv < Level::Error) return;
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    static const char* names[] = {"debug", "info", "warn", "error", "info"};
    auto tid = std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFF;
    std::lock_guard<std::mutex> lock(g_mutex);
    char line[4300];
    int n = kVerbose ? std::snprintf(line, sizeof(line), "[%9.3f] [%s] [t%04zx] %s\n", t, names[int(lv)], size_t(tid), msg)
                     : std::snprintf(line, sizeof(line), "[%9.3f] %s%s\n", t, lv == Level::Error ? "error: " : "", msg);
    n = std::min(n, int(sizeof(line)) - 1);
    if (g_file) {
        std::fwrite(line, 1, size_t(n), g_file);
        if (lv >= Level::Warn) std::fflush(g_file);
    }
    std::fwrite(line, 1, size_t(n), stderr);
}

void Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fflush(g_file);
}

}  // namespace logx
