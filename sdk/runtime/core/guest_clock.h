// The clocks the title reads run on this one. It stands still while the title is held (app::GameHeld), so nothing in
// the title sees the time jump when it goes on.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace guest_clock {

using Clock = std::chrono::steady_clock;

inline std::atomic<int64_t> g_behind_ns{0};     // how far this clock is behind the host's
inline std::atomic<int64_t> g_held_at_ns{-1};   // its value while it stands still, else negative

inline int64_t HostNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

inline Clock::time_point Now() {
    const int64_t held = g_held_at_ns.load(std::memory_order_acquire);
    const int64_t ns = held >= 0 ? held : HostNs() - g_behind_ns.load(std::memory_order_relaxed);
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns)));
}

// the title's thread alone: a reader that sees the old held value in the middle of going on still reads a value it had
inline void Hold(bool on) {
    if (on) {
        g_held_at_ns.store(HostNs() - g_behind_ns.load(std::memory_order_relaxed), std::memory_order_release);
    } else {
        g_behind_ns.store(HostNs() - g_held_at_ns.load(std::memory_order_relaxed), std::memory_order_relaxed);
        g_held_at_ns.store(-1, std::memory_order_release);
    }
}

}  // namespace guest_clock
