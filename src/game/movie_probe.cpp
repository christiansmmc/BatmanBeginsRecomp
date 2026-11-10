// Test aid for tools/movie_sweep.py, called by the [[hook]]s in config/game.toml. Each movie logs "movie open <name> <t>"
// and "movie close <name> <t>". BB_DEV_MOVIE=<file.xmv> plays that file in place of the first movie and ends the program
// when it closes; without it, the game plays as it always does.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "common/movie_path.h"
#include "common/options_screen.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"

namespace {

constexpr uint32_t kOpenPathOffset = 8;  // at 0x23E398: above the saved esi and the return address
constexpr size_t kPathCapacity = 0x1F0;  // the caller's buffer at 0x1FD870 is 0x200 bytes

std::string g_name;
bool g_redirected = false;

const char* RedirectFile() {
    static const char* file = std::getenv("BB_DEV_MOVIE");
    return file && *file ? file : nullptr;
}

double Seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// the probe's clock runs from the process's start, as close as static initialisation gets
const double g_clock_started = Seconds();

}  // namespace

void MovieProbeOpen(X86Context& ctx, uint8_t* base) {
    char* path = reinterpret_cast<char*>(base + kernel::R32(ctx.esp + kOpenPathOffset));
    if (const char* file = RedirectFile(); file && !g_redirected) {
        g_redirected = true;
        std::string redirected = RedirectMoviePath(path, file);
        if (redirected.size() < kPathCapacity) {
            std::memcpy(path, redirected.c_str(), redirected.size() + 1);
        } else {
            LOGE("BB_DEV_MOVIE is too long: %s", file);
        }
    }
    g_name = MovieFileName(path);
    LOGR("movie open %s %.3f", g_name.c_str(), Seconds());
    logx::Flush();
    options::MovieOpened();
}

void MovieProbeClose(X86Context&, uint8_t*) {
    options::MovieClosed();
    LOGR("movie close %s %.3f", g_name.c_str(), Seconds());
    logx::Flush();
    if (const char* wait = std::getenv("BB_DEV_MOVIE_TAIL_MS"); wait && RedirectFile()) {
        // for the sweep's hold mode: long enough to hear that the sound stopped with the picture
        std::thread([ms = std::atoi(wait)] {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            logx::Flush();
            kernel::Shutdown(0);
        }).detach();
        return;
    }
    if (RedirectFile()) kernel::Shutdown(0);
}

// before 0xE1972 in XMoviePlayer::Update, which reads the flag this sets. It is the flag the player's end sets, and with it
// the game takes the same path as at a video's natural end, XMoviePlayer::StopMovie at 0xE1480 (docs/TECHNICAL.md, "Videos").
void MovieSkipCheck(X86Context& ctx, uint8_t* base) {
    constexpr uint32_t kFinishedFlag = 0x8E;
    static bool holding = false;
    const bool skip = options::MovieSkipFrame();
    const bool now_holding = options::MovieSkipProgress() > 0.0f;
    if (now_holding && !holding) LOGR("movie hold %s %.3f", g_name.c_str(), Seconds());
    holding = now_holding;
    if (!skip) return;
    LOGR("movie skipped %s %.3f", g_name.c_str(), Seconds());
    base[ctx.esi + kFinishedFlag] = 1;
}
