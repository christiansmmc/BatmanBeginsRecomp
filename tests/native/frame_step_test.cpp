#include <cmath>

#include "check.h"
#include "common/frame_step.h"

namespace {
constexpr float k30 = 1.0f / 30.0f, k60 = 1.0f / 60.0f;

bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

// a value chased by the same lerp for one second
float ChaseOneSecond(float k, float frame_length) {
    float value = 0.0f;
    for (int i = 0; i < int(std::lround(1.0f / frame_length)); i++) value += (1.0f - value) * StepFraction(k, frame_length);
    return value;
}
}  // namespace

int main() {
    // at the rate the game shipped with, nothing changes, to the bit
    CHECK(StepFraction(0.075f, k30) == 0.075f);
    CHECK(FrameScale(k30) == 1.0f);
    CHECK(StepsThisFrame(0.0f, k30).steps == 1);

    // the same chase takes the same time at 60 as at 30
    CHECK(Near(ChaseOneSecond(0.075f, k60), ChaseOneSecond(0.075f, k30)));
    CHECK(Near(ChaseOneSecond(0.05f, k60), ChaseOneSecond(0.05f, k30)));
    CHECK(Near(ChaseOneSecond(0.5f, k60), ChaseOneSecond(0.5f, k30)));

    // a step for a frame half as long is half a step
    CHECK(Near(FrameScale(k60), 0.5f));

    // nothing odd at the ends
    CHECK(StepFraction(0.0f, k60) == 0.0f);
    CHECK(StepFraction(1.0f, k60) == 1.0f);
    CHECK(StepFraction(1.5f, k60) == 1.5f);   // not a lerp: left alone
    CHECK(StepFraction(-0.5f, k60) == -0.5f);

    // whole steps at 60: every other frame, and the fraction carries over
    Steps a = StepsThisFrame(0.0f, k60);
    CHECK(a.steps == 0);
    Steps b = StepsThisFrame(a.carry, k60);
    CHECK(b.steps == 1);
    CHECK(Near(b.carry, 0.0f));

    // and a slow frame can be two
    CHECK(StepsThisFrame(0.0f, k30 * 2.0f).steps == 2);

    // a counter tuned at 30 counts every other frame at 60, and every frame again once the rate is back at 30, whatever
    // the last frame at 60 left
    CounterClock clock;
    CHECK(clock.Ticks());
    clock.Frame(60.0f);
    CHECK(!clock.Ticks());
    clock.Frame(60.0f);
    CHECK(clock.Ticks());
    clock.Frame(60.0f);
    CHECK(!clock.Ticks());
    clock.Frame(30.0f);
    CHECK(clock.Ticks());
    clock.Frame(30.0f);
    CHECK(clock.Ticks());
    clock.Frame(60.0f);  // nothing carried from before the rate went down
    CHECK(!clock.Ticks());
    clock.Frame(25.0f);  // slower than 30: every frame, as the game always did
    CHECK(clock.Ticks());
    return g_failures;
}
