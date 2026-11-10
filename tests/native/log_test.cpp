// logs/batman.log in a release build: the last sessions stay, renamed at each start.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "check.h"
#include "core/log.h"

namespace fs = std::filesystem;

namespace {

fs::path NewFolder(const char* name) {
    fs::path folder = fs::temp_directory_path() / "batman_log_test" / name;
    std::error_code ignored;
    logx::Init((folder.parent_path() / "parked").string(), "parked");  // lets go of the last test's file
    fs::remove_all(folder, ignored);
    fs::create_directories(folder);
    return folder;
}

std::string Text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool Has(const fs::path& path, const std::string& words) { return Text(path).find(words) != std::string::npos; }

void Session(const fs::path& folder, int number) {
    logx::Init(folder.string(), "batman");
    logx::Write(logx::Level::Report, "session %d", number);
}

void KeepsTheLastFiveSessions() {
    fs::path folder = NewFolder("five");
    for (int i = 1; i <= 7; i++) Session(folder, i);
    CHECK(Has(folder / "batman.log", "session 7"));
    CHECK(Has(folder / "batman.1.log", "session 6"));
    CHECK(Has(folder / "batman.2.log", "session 5"));
    CHECK(Has(folder / "batman.3.log", "session 4"));
    CHECK(Has(folder / "batman.4.log", "session 3"));
    CHECK(!fs::exists(folder / "batman.5.log"));
    CHECK(!Has(folder / "batman.log", "session 6"));
}

void FirstSessionsLeaveNoEmptyFiles() {
    fs::path folder = NewFolder("first");
    Session(folder, 1);
    CHECK(fs::exists(folder / "batman.log"));
    CHECK(!fs::exists(folder / "batman.1.log"));
    Session(folder, 2);
    CHECK(Has(folder / "batman.1.log", "session 1"));
    CHECK(!fs::exists(folder / "batman.2.log"));
}

void ARelaunchIsTheSameSession() {
    fs::path folder = NewFolder("relaunch");
    Session(folder, 1);
    logx::Init(folder.string(), "batman", true);
    logx::Write(logx::Level::Report, "after the relaunch");
    CHECK(Has(folder / "batman.log", "session 1"));
    CHECK(Has(folder / "batman.log", "after the relaunch"));
    CHECK(!fs::exists(folder / "batman.1.log"));
}

void AnotherInstanceHasItsOwnFiles() {
    fs::path folder = NewFolder("instance");
    Session(folder, 1);
    logx::Init(folder.string(), "batman-2");
    logx::Write(logx::Level::Report, "second copy");
    logx::Init(folder.string(), "batman-2");
    CHECK(Has(folder / "batman.log", "session 1"));
    CHECK(Has(folder / "batman-2.1.log", "second copy"));
    CHECK(!fs::exists(folder / "batman.1.log"));
}

// a second copy of the game has the file open; the last session must not be wiped by the failed rename
void AFileThatCannotMoveIsNotLost() {
    fs::path folder = NewFolder("held");
    { std::ofstream(folder / "batman.log") << "the last session\n"; }
    HANDLE held = CreateFileW((folder / "batman.log").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    Session(folder, 2);
    CloseHandle(held);
    CHECK(Has(folder / "batman.log", "the last session"));
    CHECK(Has(folder / "batman.log", "session 2"));
}

// without a flush a line is lost with the process; this is the whole of what the log promises in a crash
void AWrittenLineLeavesTheProgramAtOnce() {
    fs::path folder = NewFolder("flush");
    logx::Init(folder.string(), "batman");
    logx::Write(logx::Level::Report, "a report line");
    CHECK(Has(folder / "batman.log", "a report line"));
    logx::Write(logx::Level::Error, "an error line");
    CHECK(Has(folder / "batman.log", "an error line"));
}

void AReleaseBuildDropsTheOtherLevels() {
    fs::path folder = NewFolder("levels");
    logx::Init(folder.string(), "batman");
    logx::Write(logx::Level::Info, "an info line");
    logx::Write(logx::Level::Warn, "a warning");
    logx::Flush();
    CHECK(!Has(folder / "batman.log", "an info line"));
    CHECK(!Has(folder / "batman.log", "a warning"));
}

}  // namespace

int main() {
    if (logx::kVerbose) return 0;  // a debug build keeps its numbered files, batman_NNN.log
    KeepsTheLastFiveSessions();
    FirstSessionsLeaveNoEmptyFiles();
    ARelaunchIsTheSameSession();
    AnotherInstanceHasItsOwnFiles();
    AFileThatCannotMoveIsNotLost();
    AWrittenLineLeavesTheProgramAtOnce();
    AReleaseBuildDropsTheOtherLevels();
    return g_failures;
}
