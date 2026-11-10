#define _CRT_SECURE_NO_WARNINGS
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "check.h"
#include "kernel/save_commit.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using kernel::SaveCommit;

namespace {

constexpr char kE[] = "\\DEVICE\\HARDDISK0\\PARTITION1";

// A call that hangs must fail this test, not hang it
struct Watchdog {
    explicit Watchdog(const char* what) {
        std::thread([done = done_, what] {
            for (int i = 0; i < 100; i++) {
                if (*done) return;
                std::this_thread::sleep_for(100ms);
            }
            std::printf("failed: %s still has not come back after 10 s\n", what);
            std::fflush(stdout);
            std::_Exit(1);
        }).detach();
    }
    ~Watchdog() { *done_ = true; }
    std::shared_ptr<std::atomic<bool>> done_ = std::make_shared<std::atomic<bool>>(false);
};

std::string Bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

void OnlyTheSaveDriveCommits() {
    for (const char* other : {"\\DEVICE\\HARDDISK0\\PARTITION2", "\\DEVICE\\HARDDISK0\\PARTITION3", "\\DEVICE\\HARDDISK0\\PARTITION4",
                              "\\DEVICE\\HARDDISK0\\PARTITION5", "\\DEVICE\\CDROM0", "\\DEVICE\\HARDDISK0\\PARTITION11", ""}) {
        SaveCommit file(other);
        file.Wrote();
        CHECK(!file.Take());
    }
    SaveCommit save(kE);
    save.Wrote();
    CHECK(save.Take());
}

void OnlyWhatWasWrittenSinceTheLastCommit() {
    SaveCommit read_only(kE);
    CHECK(!read_only.Take());  // a file only read, as the game does when it lists its saves

    SaveCommit file(kE);
    file.Wrote();
    file.Wrote();
    CHECK(file.Take());
    CHECK(!file.Take());  // the close after the title's own flush has nothing more to bring to the disk
    file.Wrote();
    CHECK(file.Take());
}

void ACommittedFileKeepsItsBytes(const fs::path& dir) {
    fs::path path = dir / "commit.bin";
    FILE* fp = std::fopen(path.string().c_str(), "w+b");
    CHECK(fp != nullptr);
    std::fwrite("save", 1, 4, fp);
    CHECK(Bytes(path).empty());  // still in the stream's buffer
    CHECK(kernel::CommitToDisk(fp));
    CHECK(Bytes(path) == "save");
    std::fwrite("more", 1, 4, fp);
    CHECK(kernel::CommitToDisk(fp));
    CHECK(Bytes(path) == "savemore");
    std::fclose(fp);
}

void ABadStreamSaysSo(const fs::path& dir) {
    fs::path path = dir / "read.bin";
    std::ofstream(path, std::ios::binary) << "x";
    FILE* fp = std::fopen(path.string().c_str(), "rb");
    CHECK(fp != nullptr);
    CHECK(!kernel::CommitToDisk(fp));  // nothing can be committed through a read only handle
    std::fclose(fp);
}

void AnOpenFileIsFlushedOnTheWayOut(const fs::path& dir) {
    fs::path path = dir / "open.bin";
    FILE* fp = std::fopen(path.string().c_str(), "w+b");
    std::fwrite("alt f4", 1, 6, fp);
    CHECK(Bytes(path).empty());
    Watchdog dog("FlushOpenFiles with free streams");
    CHECK(kernel::FlushOpenFiles(2000ms));
    CHECK(Bytes(path) == "alt f4");
    std::fclose(fp);
}

void AStreamHeldByAnotherThreadCannotHangTheExit(const fs::path& dir) {
    fs::path path = dir / "held.bin";
    FILE* fp = std::fopen(path.string().c_str(), "w+b");
    std::fwrite("held", 1, 4, fp);
    _lock_file(fp);  // as a thread stuck inside a call on this stream holds it
    auto start = std::chrono::steady_clock::now();
    {
        Watchdog dog("FlushOpenFiles with a stream held by another thread");
        std::thread([] {
            // on a thread of its own: a stream's lock lets the thread that holds it through
            CHECK(!kernel::FlushOpenFiles(300ms));
        }).join();
    }
    auto took = std::chrono::steady_clock::now() - start;
    CHECK(took >= 250ms);
    CHECK(took < 3s);
    _unlock_file(fp);  // lets the worker that stayed behind finish
    std::this_thread::sleep_for(100ms);
    std::fclose(fp);
}

}  // namespace

int main() {
    fs::path dir = fs::temp_directory_path() / "bb_save_commit_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    OnlyTheSaveDriveCommits();
    OnlyWhatWasWrittenSinceTheLastCommit();
    ACommittedFileKeepsItsBytes(dir);
    ABadStreamSaysSo(dir);
    AnOpenFileIsFlushedOnTheWayOut(dir);
    AStreamHeldByAnotherThreadCannotHangTheExit(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return g_failures;
}
