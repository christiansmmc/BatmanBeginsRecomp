// The engine steps nearly everything by the frame's length, but some game code still steps once per frame with constants
// tuned at the 30 frames per second the game shipped with. These turn such a step into the same change per second at any
// rate. At 30 they change nothing, to the bit, so the game at its own rate stays as it was. See docs/TECHNICAL.md, "Frame rate".
#pragma once
#include <cmath>

inline constexpr float kShippedFrameLength = 1.0f / 30.0f;

// how much of a frame at the shipped rate this frame is
inline float FrameScale(float frame_length) {
    return frame_length == kShippedFrameLength ? 1.0f : frame_length / kShippedFrameLength;
}

// the k of x += (target - x) * k that closes the same share of the gap per second
inline float StepFraction(float k, float frame_length) {
    if (frame_length == kShippedFrameLength || k <= 0.0f || k >= 1.0f) return k;
    return 1.0f - std::pow(1.0f - k, FrameScale(frame_length));
}

// for counters that move by whole steps: how many this frame, and what carries to the next
struct Steps {
    int steps;
    float carry;
};
inline Steps StepsThisFrame(float carry, float frame_length) {
    float total = carry + FrameScale(frame_length);
    float whole = std::floor(total + 1e-4f);
    float rest = total - whole;
    return {int(whole), rest < 0.0f ? 0.0f : rest};
}

// Whether a frame counter tuned at the shipped rate counts this frame: above 30, once per shipped frame's worth of time; at
// 30 or slower, every frame, as the game always did, whatever the frames at a faster rate left behind.
class CounterClock {
public:
    void Frame(float rate) {
        if (rate <= 30.0f) {
            tick_ = true;
            carry_ = 0.0f;
            return;
        }
        const Steps steps = StepsThisFrame(carry_, 1.0f / rate);
        tick_ = steps.steps > 0;
        carry_ = steps.carry;
    }
    bool Ticks() const { return tick_; }

private:
    bool tick_ = true;
    float carry_ = 0.0f;
};
