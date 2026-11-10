#include "common/movie_skip.h"

#include <algorithm>

namespace {
constexpr double kHoldSeconds = 1.0;
}

bool MovieSkip::Update(bool held, double seconds) {
    if (waiting_for_release_) {
        waiting_for_release_ = held;
        return false;
    }
    held_seconds_ = held ? held_seconds_ + seconds : 0.0;
    return held_seconds_ >= kHoldSeconds;
}

float MovieSkip::Progress() const { return float(std::min(held_seconds_ / kHoldSeconds, 1.0)); }
