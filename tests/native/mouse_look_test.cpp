#include <cmath>

#include "check.h"
#include "common/mouse_look_math.h"

using namespace mouse;

namespace {

bool Near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }

constexpr float kDegree = 0.017453292f;
constexpr Tuning kTuning{0.08f * kDegree, false, false};
// [0x30FF90] is 2 at 30 FPS and 1 at 60 in normal play
constexpr CameraTerms k30{1.0f, 1.0f, 2.0f};   // the game's camera options off: gains +1
constexpr CameraTerms k60{1.0f, 1.0f, 1.0f};
constexpr CameraTerms kOn30{-1.0f, -1.0f, 2.0f};  // the game's camera options on: gains -1
constexpr CameraTerms kOn60{-1.0f, -1.0f, 1.0f};
constexpr CameraTerms kSlow{1.0f, -1.0f, 0.5f};  // slow motion, one option on
constexpr CameraTerms kOther{1.7f, 0.6f, 1.0f};
constexpr CameraTerms kAll[] = {k30, k60, kOn30, kOn60, kSlow, kOther};

void TestRoundTrip() {
    for (const CameraTerms& c : kAll) {
        for (float rate : {0.05f, 0.01f, 0.001f, -0.002f, -0.02f, -0.3f}) {
            CHECK(Near(YawRateOf(StickForYawRate(rate, c), c), rate));
            CHECK(Near(PitchRateOf(StickForPitchRate(rate, c), c), rate));
        }
    }
    CHECK(StickForPitchRate(0.0f, k30) == 0.0f);
    CHECK(StickForPitchRate(0.0f, kOn30) == 0.0f);
}

// the stick's own pitch: nothing inside the dead band, and the sign follows the stick and the gain
void TestPitchDeadBand() {
    CHECK(PitchRateOf(0.5f, k60) == 0.0f);     // 0.5 * 0.0506 is under 0.027
    CHECK(PitchRateOf(-0.5f, k60) == 0.0f);
    CHECK(PitchRateOf(1.0f, k60) > 0.0f);
    CHECK(PitchRateOf(-1.0f, k60) < 0.0f);
    CHECK(PitchRateOf(1.0f, kOn60) < 0.0f);
    CHECK(Near(PitchRateOf(1.0f, k60), kPitchScale - kPitchDeadBand));
    CHECK(Near(PitchRateOf(1.0f, k30), 2.0f * (kPitchScale - kPitchDeadBand)));
}

// no mouse movement changes nothing, to the bit
void TestNoMovement() {
    Aim aim;
    const Sticks stick{0.3f, -0.8f};
    const Sticks out = aim.Apply(stick, 0.0f, 0.0f, k30, kTuning);
    CHECK(out.x == stick.x && out.y == stick.y);
}

// the camera turns by the mouse's angle, whatever its gain and the frame rate: to the right (a negative yaw rate) and down (a
// negative pitch rate)
void TestAngleIsWhatTheMouseTravelled() {
    for (const CameraTerms& c : kAll) {
        Aim aim;
        const Sticks out = aim.Apply({0.0f, 0.0f}, 40.0f, 25.0f, c, kTuning);
        CHECK(Near(YawRateOf(out.x, c), -40.0f * kTuning.radians_per_count, 1e-4f));
        CHECK(Near(PitchRateOf(out.y, c), -25.0f * kTuning.radians_per_count, 1e-4f));
    }
}

// with the game's vertical camera option on, a small vertical move is not lost in the dead band
void TestSmallPitchWithTheOptionOn() {
    for (const CameraTerms& c : {kOn30, kOn60}) {
        for (float counts : {1.0f, 3.0f, -2.0f, 10.0f}) {
            Aim aim;
            const Sticks out = aim.Apply({0.0f, 0.0f}, 0.0f, counts, c, kTuning);
            CHECK(Near(PitchRateOf(out.y, c), -counts * kTuning.radians_per_count, 1e-5f));
        }
    }
}

// with the defaults the view goes the way the mouse goes, whatever the game's camera options; inverting flips one axis
void TestSigns() {
    for (const CameraTerms& c : {k30, kOn30}) {
        Aim aim;
        CHECK(YawRateOf(aim.Apply({0.0f, 0.0f}, 30.0f, 0.0f, c, kTuning).x, c) < 0.0f);  // right
        aim.Reset();
        CHECK(YawRateOf(aim.Apply({0.0f, 0.0f}, -30.0f, 0.0f, c, kTuning).x, c) > 0.0f);  // left
        aim.Reset();
        CHECK(PitchRateOf(aim.Apply({0.0f, 0.0f}, 0.0f, 30.0f, c, kTuning).y, c) < 0.0f);  // down
        aim.Reset();
        CHECK(PitchRateOf(aim.Apply({0.0f, 0.0f}, 0.0f, -30.0f, c, kTuning).y, c) > 0.0f);  // up
        aim.Reset();
        const Sticks x = aim.Apply({0.0f, 0.0f}, 30.0f, 0.0f, c, {kTuning.radians_per_count, true, false});
        CHECK(YawRateOf(x.x, c) > 0.0f);
        aim.Reset();
        const Sticks y = aim.Apply({0.0f, 0.0f}, 0.0f, 30.0f, c, {kTuning.radians_per_count, false, true});
        CHECK(PitchRateOf(y.y, c) > 0.0f);
    }
    // the stick's direction is what changes with the option: off, the stick goes left to turn right; on, right
    Aim aim;
    CHECK(aim.Apply({0.0f, 0.0f}, 30.0f, 0.0f, k30, kTuning).x < 0.0f);
    aim.Reset();
    CHECK(aim.Apply({0.0f, 0.0f}, 30.0f, 0.0f, kOn30, kTuning).x > 0.0f);
}

// a slow move is under the game's floor each frame: nothing is lost, it is given in one go
void TestSlowMovesGather() {
    for (const CameraTerms& c : {k30, k60}) {
        Aim aim;
        float turned = 0.0f;
        int frames_given = 0;
        for (int frame = 0; frame < 200; frame++) {
            const Sticks out = aim.Apply({0.0f, 0.0f}, 1.0f, 0.0f, c, kTuning);  // one count a frame
            const float rate = YawRateOf(out.x, c);
            if (std::fabs(rate) >= kYawFloor) {  // what the game keeps
                turned += rate;
                frames_given++;
            }
        }
        const float wanted = -200.0f * kTuning.radians_per_count;
        CHECK(frames_given > 0);
        CHECK(Near(turned + aim.PendingYaw(), wanted, 1e-4f));
        CHECK(std::fabs(aim.PendingYaw()) < kYawFloor * 1.01f);
    }
}

// the stick and the mouse add up
void TestStickAndMouseAdd() {
    Aim aim;
    const float stick_only = YawRateOf(0.6f, k30);
    const Sticks out = aim.Apply({0.6f, 0.0f}, 20.0f, 0.0f, k30, kTuning);
    CHECK(Near(YawRateOf(out.x, k30), stick_only - 20.0f * kTuning.radians_per_count, 1e-4f));

    for (const CameraTerms& c : {k30, kOn30}) {
        Aim pitch;
        const float up_only = PitchRateOf(-1.0f, c);
        const Sticks both = pitch.Apply({0.0f, -1.0f}, 0.0f, 15.0f, c, kTuning);
        CHECK(Near(PitchRateOf(both.y, c), up_only - 15.0f * kTuning.radians_per_count, 1e-4f));
    }
}

// the game at a standstill: nothing is divided by the factor, and the move waits
void TestStandstill() {
    Aim aim;
    const CameraTerms frozen{1.0f, 1.0f, 0.0f};
    const Sticks stick{0.1f, 0.2f};
    const Sticks out = aim.Apply(stick, 50.0f, 50.0f, frozen, kTuning);
    CHECK(out.x == stick.x && out.y == stick.y);
    CHECK(aim.PendingYaw() < 0.0f);
    const Sticks later = aim.Apply(stick, 0.0f, 0.0f, k30, kTuning);  // the game runs again: it is given
    CHECK(later.x < stick.x);
}

// a camera with no gain takes no input: nothing to divide by
void TestNoGain() {
    Aim aim;
    const CameraTerms none{0.0f, 0.0f, 1.0f};
    const Sticks out = aim.Apply({0.0f, 0.0f}, 50.0f, 50.0f, none, kTuning);
    CHECK(out.x == 0.0f && out.y == 0.0f);
}

// a very large move is capped, and a reset drops what waits
void TestLimits() {
    Aim aim;
    aim.Apply({0.0f, 0.0f}, 1e9f, 0.0f, {1.0f, 1.0f, 0.0f}, kTuning);
    CHECK(Near(aim.PendingYaw(), -kMaxPending));
    aim.Reset();
    CHECK(aim.PendingYaw() == 0.0f);
}

}  // namespace

int main() {
    TestRoundTrip();
    TestPitchDeadBand();
    TestNoMovement();
    TestAngleIsWhatTheMouseTravelled();
    TestSmallPitchWithTheOptionOn();
    TestSigns();
    TestSlowMovesGather();
    TestStickAndMouseAdd();
    TestStandstill();
    TestNoGain();
    TestLimits();
    return g_failures;
}
