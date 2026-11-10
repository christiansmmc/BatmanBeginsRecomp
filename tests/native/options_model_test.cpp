#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "common/keyboard_pad.h"
#include "common/options_model.h"
#include "core/input.h"

using namespace options;

namespace {

constexpr uint32_t kVkEscape = 0x1B, kVkLeftWindows = 0x5B, kVkF3 = 0x72;
constexpr uint16_t kStartBit = 1 << 4, kBackBit = 1 << 5, kUpBit = 1 << 0;
constexpr int kA = 0, kB = 1, kLeftTrigger = 6;

bool Neutral(const input::Pad& pad) {
    input::Pad zero;
    return pad.buttons == 0 && !std::memcmp(pad.analog, zero.analog, sizeof(zero.analog)) && pad.lx == 0 && pad.ly == 0 &&
           pad.rx == 0 && pad.ry == 0;
}

input::Pad Pressed(int analog) {
    input::Pad pad;
    pad.connected = true;
    pad.analog[analog] = 255;
    return pad;
}

input::Pad Buttons(uint16_t buttons) {
    input::Pad pad;
    pad.connected = true;
    pad.buttons = buttons;
    return pad;
}

void TestRenderPercent() {
    CHECK(RenderPercentFor(3, 1440, 480) == 100);
    CHECK(RenderPercentFor(4, 1440, 480) == 134);  // up, never under the asked scale
    CHECK(RenderPercentFor(2, 720, 480) == 134);
    CHECK(RenderPercentFor(3, 720, 480) == 200);
    CHECK(RenderPercentFor(2, 1080, 480) == 89);
    CHECK(RenderPercentFor(0, 720, 480) == 100);  // nothing to divide by: the window's own height
    CHECK(RenderPercentFor(3, 0, 480) == 100);
}

void TestScaleLabel() {
    CHECK(ScaleLabel(4, 640, 480) == "4x · 2560 x 1920");
    CHECK(ScaleLabel(2, 640, 480) == "2x · 1280 x 960");
}

void TestScaleChoices() {
    // a window of 720 lines: 100% is 1.5x, its own entry below the whole scales
    std::vector<ScaleChoice> at720 = ScaleChoices(100, 720, 640, 480);
    CHECK(at720.size() == 4);
    CHECK(at720[0].percent == 100);
    CHECK(at720[0].label == "AUTO · 960 x 720");
    CHECK(at720[1].percent == 134);
    CHECK(at720[1].label == "2x · 1280 x 960");
    CHECK(at720[2].percent == 200);
    CHECK(at720[3].percent == 267);

    // at 1440 lines the window's height is 3x, so there is no entry of its own
    std::vector<ScaleChoice> at1440 = ScaleChoices(100, 1440, 640, 480);
    CHECK(at1440.size() == 3);
    CHECK(at1440[0].percent == 67);
    CHECK(at1440[1].percent == 100);
    CHECK(at1440[1].label == "3x · 1920 x 1440");
    CHECK(at1440[2].percent == 134);

    // a value set by hand in the settings file shows as it is, and can be chosen again
    std::vector<ScaleChoice> custom = ScaleChoices(300, 720, 640, 480);
    CHECK(custom.size() == 5);
    CHECK(custom[4].percent == 300);
    CHECK(custom[4].label == "300% · 2880 x 2160");
}

void TestPercentKeepsTheScaleWhenTheOutputChanges() {
    CHECK(PercentForNewOutput(134, 720, 1440, 480) == 67);    // 2x stays 2x
    CHECK(PercentForNewOutput(100, 1440, 720, 480) == 100);   // the window's own height stays that
    CHECK(PercentForNewOutput(100, 720, 1440, 480) == 100);
    CHECK(PercentForNewOutput(300, 720, 1440, 480) == 300);   // a hand set value is left alone
}

void TestRebindSwapsAKeyInUse() {
    KeyMap map = kDefaultKeyMap;
    const uint32_t old_punch = map[Action::Punch];
    CHECK(old_punch == 'E');
    CHECK(map[Action::Kick] == 'Q');
    CHECK(RebindKey(map, Action::Punch, 'Q'));
    CHECK(map[Action::Punch] == 'Q');
    CHECK(map[Action::Kick] == old_punch);
}

void TestRebindAFreeKey() {
    KeyMap map = kDefaultKeyMap;
    CHECK(RebindKey(map, Action::Punch, 'P'));
    CHECK(map[Action::Punch] == 'P');
    CHECK(map[Action::Kick] == 'Q');
    CHECK(RebindKey(map, Action::Punch, 'P'));  // the same key again changes nothing
    CHECK(map[Action::Punch] == 'P');
}

void TestRebindRefusesKeysThatAreNotTheGames() {
    KeyMap map = kDefaultKeyMap;
    CHECK(!RebindKey(map, Action::Punch, kVkEscape));  // Esc is the screen's
    CHECK(!RebindKey(map, Action::Punch, kVkF3));      // the log's thread dump
    CHECK(!RebindKey(map, Action::Punch, kVkLeftWindows));
    CHECK(!RebindKey(map, Action::Punch, 0));
    CHECK(map[Action::Punch] == 'E');
}

void TestKeyMapCheckedOnLoad() {
    CHECK(ValidKeyMap(kDefaultKeyMap));
    KeyMap twice = kDefaultKeyMap;
    twice[Action::Punch] = twice[Action::Kick];
    CHECK(!ValidKeyMap(twice));
    KeyMap reserved = kDefaultKeyMap;
    reserved[Action::Punch] = kVkEscape;
    CHECK(!ValidKeyMap(reserved));
    KeyMap none = kDefaultKeyMap;
    none[Action::Punch] = 0;
    CHECK(!ValidKeyMap(none));
}

void TestKeyNames() {
    CHECK(KeyName('E') == "E");
    CHECK(KeyName('7') == "7");
    CHECK(KeyName(0x20) == "SPACE");
    CHECK(KeyName(0x0D) == "ENTER");
    CHECK(KeyName(0x26) == "UP");
    CHECK(KeyName(0x28) == "DOWN");
    CHECK(KeyName(0x25) == "LEFT");
    CHECK(KeyName(0x27) == "RIGHT");
    CHECK(KeyName(0xA0) == "LEFT SHIFT");
    CHECK(KeyName(0xA1) == "RIGHT SHIFT");
    CHECK(KeyName(0xA2) == "LEFT CTRL");
    CHECK(KeyName(0xA3) == "RIGHT CTRL");
    CHECK(KeyName(0xA4) == "LEFT ALT");
    CHECK(KeyName(0xA5) == "RIGHT ALT");
    CHECK(KeyName(0x71) == "F2");
    CHECK(KeyName(0xFF) == "KEY 255");  // a key with no name of its own
}

void TestScreenOpenGivesTheGameANeutralPad() {
    PadGate gate;
    input::Pad pad = Pressed(kA);
    pad.buttons = kStartBit | kUpBit;
    pad.lx = 20000;
    pad.ry = -20000;
    pad.analog[kLeftTrigger] = 200;
    gate.Apply(true, pad);
    CHECK(pad.connected);  // a controller stays plugged in, or the game would stop to ask for it
    CHECK(Neutral(pad));

    input::Pad unplugged;
    gate.Apply(true, unplugged);
    CHECK(!unplugged.connected);
}

void TestButtonHeldAtCloseWaitsForRelease() {
    PadGate gate;
    input::Pad held = Pressed(kB);
    gate.Apply(true, held);

    input::Pad after = Pressed(kB);
    gate.Apply(false, after);  // closed, B still down
    CHECK(after.analog[kB] == 0);

    input::Pad other = Pressed(kB);
    other.analog[kA] = 255;
    gate.Apply(false, other);  // A was pressed after the close, so it counts; B still waits
    CHECK(other.analog[kA] == 255);
    CHECK(other.analog[kB] == 0);

    input::Pad up;
    gate.Apply(false, up);

    input::Pad again = Pressed(kB);
    gate.Apply(false, again);  // pressed anew
    CHECK(again.analog[kB] == 255);
}

void TestStickHeldAtCloseWaitsForCentre() {
    PadGate gate;
    input::Pad held;
    held.lx = 30000;
    gate.Apply(true, held);

    input::Pad after;
    after.lx = 30000;
    gate.Apply(false, after);
    CHECK(after.lx == 0);

    input::Pad centred;
    centred.lx = 500;  // resting stick noise is not held
    gate.Apply(false, centred);
    CHECK(centred.lx == 500);

    input::Pad pushed;
    pushed.lx = 30000;
    gate.Apply(false, pushed);
    CHECK(pushed.lx == 30000);
}

void TestStartHiddenWhileBackIsDown() {
    PadGate gate;
    input::Pad both = Buttons(kBackBit | kStartBit);
    gate.Apply(false, both);
    CHECK(!(both.buttons & kStartBit));

    input::Pad start_only = Buttons(kStartBit);
    gate.Apply(false, start_only);  // Back let go first: Start was never a press of its own
    CHECK(!(start_only.buttons & kStartBit));

    input::Pad none;
    gate.Apply(false, none);

    input::Pad start_again = Buttons(kStartBit);
    gate.Apply(false, start_again);
    CHECK(start_again.buttons & kStartBit);

    input::Pad back_only = Buttons(kBackBit);
    gate.Apply(false, back_only);
    CHECK(back_only.buttons & kBackBit);  // alone it is the game's
}

void TestBlockHeld() {
    PadGate gate;
    input::Pad pad = Pressed(kA);
    gate.Apply(false, pad);
    CHECK(pad.analog[kA] == 255);  // the game sees it while a video plays

    gate.BlockHeld();  // the video was skipped
    input::Pad still = Pressed(kA);
    gate.Apply(false, still);
    CHECK(still.analog[kA] == 0);

    input::Pad up;
    gate.Apply(false, up);
    input::Pad next = Pressed(kA);
    gate.Apply(false, next);
    CHECK(next.analog[kA] == 255);
}

void TestChord() {
    Chord chord;
    CHECK(!chord.Update(false));
    CHECK(chord.Update(true));   // once when it is made
    CHECK(!chord.Update(true));
    CHECK(!chord.Update(false));
    CHECK(chord.Update(true));
}

void TestNavRepeat() {
    NavRepeat repeat;
    CHECK(repeat.Update(true, 0.016));    // at once when pressed
    CHECK(!repeat.Update(true, 0.1));
    CHECK(!repeat.Update(true, 0.2));
    CHECK(repeat.Update(true, 0.1));      // then after the first delay
    CHECK(!repeat.Update(true, 0.05));
    CHECK(repeat.Update(true, 0.1));      // and on
    CHECK(!repeat.Update(false, 0.1));
    CHECK(repeat.Update(true, 0.016));
}

void TestWindowSizes() {
    std::vector<WindowSize> sizes = WindowSizes(2560, 1440, 1280, 720, false);
    CHECK(sizes.size() == 4);
    CHECK(sizes[0].width == 1280 && sizes[0].height == 720);
    CHECK(sizes[3].width == 2560 && sizes[3].height == 1440);

    // a window needs room for its title bar
    std::vector<WindowSize> windowed = WindowSizes(2560, 1440, 1280, 720, true);
    CHECK(windowed.size() == 3);
    CHECK(windowed[2].width == 1920 && windowed[2].height == 1080);

    std::vector<WindowSize> small = WindowSizes(1600, 900, 1280, 720, false);
    CHECK(small.size() == 2);

    // one set by hand in the settings file stays on the list
    std::vector<WindowSize> own = WindowSizes(2560, 1440, 960, 720, true);
    CHECK(own.size() == 4);
    CHECK(own[0].width == 960 && own[0].height == 720);
}

}  // namespace

int main() {
    TestRenderPercent();
    TestScaleLabel();
    TestScaleChoices();
    TestPercentKeepsTheScaleWhenTheOutputChanges();
    TestRebindSwapsAKeyInUse();
    TestRebindAFreeKey();
    TestRebindRefusesKeysThatAreNotTheGames();
    TestKeyMapCheckedOnLoad();
    TestKeyNames();
    TestScreenOpenGivesTheGameANeutralPad();
    TestButtonHeldAtCloseWaitsForRelease();
    TestStickHeldAtCloseWaitsForCentre();
    TestStartHiddenWhileBackIsDown();
    TestBlockHeld();
    TestChord();
    TestNavRepeat();
    TestWindowSizes();
    return g_failures;
}
