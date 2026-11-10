#include "check.h"
#include "common/fps_policy.h"

int main() {
    CHECK(EffectiveFpsCap(0, 144, 60) == 60);    // no cap asked: the highest validated rate
    CHECK(EffectiveFpsCap(1, 144, 60) == 60);    // kFpsCapRefresh on a faster monitor
    CHECK(EffectiveFpsCap(1, 50, 60) == 50);     // kFpsCapRefresh on a slower monitor
    CHECK(EffectiveFpsCap(144, 144, 60) == 60);  // above what was validated
    CHECK(EffectiveFpsCap(30, 144, 60) == 30);   // below it
    CHECK(EffectiveFpsCap(1, 144, 30) == 30);    // nothing above 30 validated

    // the rate the game asks for in play: its own, unless the trial setting asks for 60
    CHECK(PlayFrameRate(0) == kShippedPlayFps);
    CHECK(PlayFrameRate(30) == kShippedPlayFps);
    CHECK(PlayFrameRate(45) == kShippedPlayFps);   // the engine's clock only divides into 60, 30: not a rate it can hold
    CHECK(PlayFrameRate(144) == kShippedPlayFps);  // nothing above 60 was tried
    CHECK(PlayFrameRate(60) == 60);

    // the rate in play holds for the whole run: a choice on the options screen is saved and waits for the next start
    PlayRateSetting setting;
    CHECK(setting.InForce() == 0);
    CHECK(setting.Chosen() == 0);
    CHECK(!setting.WaitsForRestart());
    setting.Load(60);
    CHECK(setting.InForce() == 60);
    CHECK(setting.Chosen() == 60);
    CHECK(!setting.WaitsForRestart());
    setting.Choose(0);
    CHECK(setting.InForce() == 60);  // the game keeps the rate it started with
    CHECK(setting.Chosen() == 0);    // and the file gets the new one
    CHECK(setting.WaitsForRestart());
    setting.Choose(60);
    CHECK(!setting.WaitsForRestart());
    setting.Load(0);
    setting.Choose(60);
    CHECK(setting.InForce() == 0);
    CHECK(setting.Chosen() == 60);
    CHECK(setting.WaitsForRestart());
    setting.Load(45);  // not a rate the screen offers: the game's own
    CHECK(setting.InForce() == 0);
    CHECK(setting.Chosen() == 0);
    setting.Choose(144);
    CHECK(setting.Chosen() == 0);
    return g_failures;
}
