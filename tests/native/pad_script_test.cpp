#include <stdexcept>
#include <string>

#include "check.h"
#include "common/pad_script.h"
#include "core/input.h"

static void TestScriptTiming() {
    auto s = ParsePadScript("120 A\n300 START+A 20 # x\n");
    CHECK(ButtonsAtFrame(s, 119) == 0);
    CHECK(ButtonsAtFrame(s, 120) == kA);
    CHECK(ButtonsAtFrame(s, 127) == kA);
    CHECK(ButtonsAtFrame(s, 128) == 0);
    CHECK(ButtonsAtFrame(s, 300) == (kStart | kA));
    CHECK(ButtonsAtFrame(s, 319) == (kStart | kA));
    CHECK(ButtonsAtFrame(s, 320) == 0);
}

static void TestParsing() {
    CHECK(ParsePadScript("# only a comment\n\n").empty());
    CHECK(ParsePadScript("5 LSD 2\n")[0].buttons == kLeftStickDown);
    CHECK_THROWS(ParsePadScript("10 NOPE"));
    CHECK_THROWS(ParsePadScript("ten A"));
    try {
        ParsePadScript("1 A\n2 NOPE\n");
        CHECK(false);
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("line 2") != std::string::npos);
    }
}

static void TestOverlappingPressesCombine() {
    auto s = ParsePadScript("10 A 10\n15 B 10\n");
    CHECK(ButtonsAtFrame(s, 16) == (kA | kB));
    CHECK(ButtonsAtFrame(s, 22) == kB);
}

static void TestApplyToPad() {
    input::Pad pad;
    ApplyButtons(kStart | kA | kLeftStick | kLeftTrigger, pad);
    CHECK(pad.connected);
    CHECK(pad.buttons == 16);
    CHECK(pad.analog[0] == 255);
    CHECK(pad.analog[1] == 0);
    CHECK(pad.analog[6] == 255);
    CHECK(pad.ly == 32767);

    input::Pad kept;
    kept.analog[1] = 200;
    ApplyButtons(0, kept);
    CHECK(!kept.connected);
    CHECK(kept.analog[1] == 200);
}

int main() {
    TestScriptTiming();
    TestParsing();
    TestOverlappingPressesCombine();
    TestApplyToPad();
    return g_failures;
}
