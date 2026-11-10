// Bringing the game's saves to the disk: flushing or closing a FILE only hands its bytes to Windows' cache, which a power cut
// loses.
#pragma once
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kernel {

// E:, hdd\E beside the exe, where the player's saves are
inline constexpr std::string_view kSaveDevice = "\\DEVICE\\HARDDISK0\\PARTITION1";

// Whether a file's next flush or close has to reach the disk: it is on the save drive, and written since the last commit.
// A commit takes milliseconds, which a save can spare and a utility or cache drive's frequent writes could not.
class SaveCommit {
public:
    SaveCommit() = default;
    explicit SaveCommit(std::string_view device) : save_drive_(device == kSaveDevice) {}
    void Wrote() { written_ = true; }
    bool Take() {
        bool due = save_drive_ && written_;
        written_ = false;
        return due;
    }

private:
    bool save_drive_ = false;
    bool written_ = false;
};

// false when the bytes did not get there
inline bool CommitToDisk(FILE* fp) {
    if (std::fflush(fp) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(fp)) == 0;
#else
    return fsync(fileno(fp)) == 0;
#endif
}

// What the open files still hold in their buffers goes to Windows before leaving by _Exit, which otherwise rests on the C
// runtime's own cleanup as the process ends (seen to work with this build's runtime). The flush needs each stream's lock,
// which a thread stuck in a call on that stream holds, so it runs on a thread of its own: past the limit the program leaves
// without it (false), and whatever else that thread holds up, the closed window still closes.
inline bool FlushOpenFiles(std::chrono::milliseconds limit) {
    auto flushed = std::make_shared<std::promise<void>>();
    std::future<void> done = flushed->get_future();
    std::thread([flushed] {
#ifdef _WIN32
        _flushall();
#else
        std::fflush(nullptr);
#endif
        flushed->set_value();
    }).detach();
    return done.wait_for(limit) == std::future_status::ready;
}

}  // namespace kernel
