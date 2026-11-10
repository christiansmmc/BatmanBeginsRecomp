// Only a frame rate that was checked against the game's real speed may be used.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

// A ceiling, not the game's speed: the game picks its own rate, 60 while a video plays and 30 everywhere else. The rate in
// play has its own limit below, and the checks are in docs/TECHNICAL.md, "Frame rate".
inline constexpr uint32_t kMaxValidatedFps = 60;

// requested as app::Settings::fps_cap has it: 0 unlimited, 1 the monitor's refresh rate, else frames per second
inline uint32_t EffectiveFpsCap(uint32_t requested, uint32_t refresh_hz, uint32_t max_validated) {
    if (requested == 0) return max_validated;
    if (requested == 1) return std::min(refresh_hz, max_validated);
    return std::min(requested, max_validated);
}

inline constexpr uint32_t kShippedPlayFps = 30;

// The highest rate in play that passed the speed checks. 60 did not: too many steps tuned per frame remain, some in places
// that cannot be measured here. See docs/TECHNICAL.md, "Frame rate".
inline constexpr uint32_t kMaxValidatedPlayFps = kShippedPlayFps;

// The whole campaign has been played at 60, so the options screen offers it, marked as experimental, since the steps
// above still run fast in places, the helicopter among them. See docs/TECHNICAL.md, "60 FPS option".
inline constexpr uint32_t kExperimentalPlayFps = 60;

// The rate the game asks for in play, m_MaxFrameRate in the engine: experimental_play_fps in the settings file, which the
// options screen sets, 60 or else the game's own.
inline uint32_t PlayFrameRate(uint32_t experimental) {
    return experimental == kExperimentalPlayFps ? kExperimentalPlayFps : kMaxValidatedPlayFps;
}

// The rate in play holds for the whole run. The game works values out of it as it makes objects, such as per-frame steps
// from per-second ones (0x1E1B0), so a change in the middle of a level would leave those at the old rate, a state nobody
// played. A choice on the options screen is saved and takes effect at the next start, as the campaign was played.
class PlayRateSetting {
public:
    // experimental_play_fps from the settings file, before the game starts
    void Load(uint32_t fps) {
        in_force_ = Offered(fps);
        chosen_ = in_force_;
    }
    void Choose(uint32_t fps) { chosen_ = Offered(fps); }
    uint32_t InForce() const { return in_force_; }  // what this run plays at: 60, or 0 for the game's own
    uint32_t Chosen() const { return chosen_; }     // what the settings file gets
    bool WaitsForRestart() const { return chosen_ != in_force_; }

private:
    static uint32_t Offered(uint32_t fps) { return fps == kExperimentalPlayFps ? fps : 0; }

    uint32_t in_force_ = 0;
    std::atomic<uint32_t> chosen_{0};  // the screen sets it, and the settings may be saved from another thread
};
