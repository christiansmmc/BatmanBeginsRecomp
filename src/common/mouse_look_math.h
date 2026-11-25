// Mouse look for the camera. The camera's input function (0x33A70) turns the right stick into this frame's turn, with a
// gain, a dead band and the game's frame time factor in it. The mouse does not move a stick: it moves the camera by the angle
// it travelled, so these work out which stick value makes that function produce exactly that angle, with the game's own locks,
// limits and automatic return still in force. See docs/TECHNICAL.md, "Mouse look".
#pragma once
#include <algorithm>
#include <cmath>

namespace mouse {

// the constants the game's function reads
inline constexpr float kYawScale = 0.047996554f;   // 0x2E5B2C: turn per frame at full stick, before the camera's gain
inline constexpr float kPitchScale = 0.05061455f;  // 0x2E5B28
inline constexpr float kPitchDeadBand = 0.027f;    // 0x2E5B24, taken off the pitch's size
inline constexpr float kYawFloor = 0.006f;         // 0x2E5B30, a turn smaller than this per frame is dropped

// a mouse move the camera cannot take yet waits for the next frames, up to this much (radians)
inline constexpr float kMaxPending = 0.5f;
// below this the frame time factor is the game at a standstill, and nothing is divided by it
inline constexpr float kMinSlowMotion = 0.01f;

// what the camera multiplies the stick by: [camera+0x864] and [camera+0x868], whose sign is the game's own horizontal and
// vertical camera options (-1 when on, +1 when off, written at 0x257B6), and [0x30FF90], m_GameRenderFrameRateMul: the
// game's slow motion times 60 / rate, so 2 at 30 FPS and 1 at 60 in normal play. The turn is per frame and scaled by it, and
// the functions below divide by it, so the mouse's angle does not depend on the frame rate.
struct CameraTerms {
    float yaw_gain;
    float pitch_gain;
    float slow_motion;
};

// the stick as the game holds it after its own dead zone: x is positive to the right, y is negative upwards
struct Sticks {
    float x;
    float y;
};

// invert_x / invert_y false: the view goes the way the mouse goes (right turns right, down looks down)
struct Tuning {
    float radians_per_count;
    bool invert_x;
    bool invert_y;
};

// The turn per frame (radians) the camera takes from a stick value, and the stick value that gives a turn. Whatever the gains'
// sign, a positive yaw rate turns the view left and a positive pitch rate looks up: the gain only decides which way the stick
// has to go for it.
inline float YawRateOf(float x, const CameraTerms& c) { return c.yaw_gain * c.slow_motion * x * kYawScale; }
inline float StickForYawRate(float rate, const CameraTerms& c) { return rate / (c.yaw_gain * c.slow_motion * kYawScale); }

inline float PitchRateOf(float y, const CameraTerms& c) {
    const float size = std::max(0.0f, std::fabs(y * kPitchScale) - kPitchDeadBand);
    const float rate = size * c.pitch_gain * c.slow_motion;
    return y < 0.0f ? -rate : rate;
}
// the dead band is taken off the size before the gain, so the size comes from the gain's magnitude and the stick's sign from
// the rate's and the gain's together
inline float StickForPitchRate(float rate, const CameraTerms& c) {
    if (rate == 0.0f) return 0.0f;
    const float size = std::fabs(rate) / (std::fabs(c.pitch_gain) * c.slow_motion) + kPitchDeadBand;
    const float y = size / kPitchScale;
    return (rate < 0.0f) != (c.pitch_gain < 0.0f) ? -y : y;
}

// Adds what the mouse moved to the stick the camera is about to read. The title's thread only.
class Aim {
public:
    // dx and dy are the mouse counts since the last frame, to the right and downwards. The result is the stick to give the
    // camera: the same as the one given when the mouse did not move.
    Sticks Apply(Sticks stick, float dx, float dy, const CameraTerms& c, const Tuning& t) {
        // a positive rate is to the left and upwards, so the mouse's right and down are negative unless inverted
        yaw_ = std::clamp(yaw_ + dx * t.radians_per_count * (t.invert_x ? 1.0f : -1.0f), -kMaxPending, kMaxPending);
        pitch_ = std::clamp(pitch_ + dy * t.radians_per_count * (t.invert_y ? 1.0f : -1.0f), -kMaxPending, kMaxPending);
        if (c.slow_motion < kMinSlowMotion) return stick;

        if (yaw_ != 0.0f && c.yaw_gain != 0.0f) {
            const float total = YawRateOf(stick.x, c) + yaw_;
            // slow moves would be dropped by the game's floor every frame: they gather until they are over it
            if (std::fabs(total) >= kYawFloor * 1.01f) {
                stick.x = StickForYawRate(total, c);
                yaw_ = 0.0f;
            }
        }
        if (pitch_ != 0.0f && c.pitch_gain != 0.0f) {
            stick.y = StickForPitchRate(PitchRateOf(stick.y, c) + pitch_, c);
            pitch_ = 0.0f;
        }
        return stick;
    }

    void Reset() { yaw_ = pitch_ = 0.0f; }
    float PendingYaw() const { return yaw_; }

private:
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
};

}  // namespace mouse
