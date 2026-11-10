#include <cstring>
#include <initializer_list>

#include "check.h"
#include "common/keyboard_pad.h"
#include "core/input.h"

namespace {

constexpr uint32_t kKeyDownMessage = 0x0100, kKeyUpMessage = 0x0101, kKillFocusMessage = 0x0008;
constexpr uintptr_t kVkShift = 0x10, kVkControl = 0x11;
constexpr intptr_t kExtended = intptr_t(1) << 24;

KeyState Held(std::initializer_list<Action> keys) {
    KeyState state;
    for (Action action : keys) state.Set(action, true);
    return state;
}

intptr_t Scan(uint32_t code) { return intptr_t(code) << 16; }

input::Pad Applied(std::initializer_list<Action> keys) {
    input::Pad pad;
    ApplyKeyboard(Held(keys), pad);
    return pad;
}

bool SameReport(const input::Pad& a, const input::Pad& b) {
    return a.connected == b.connected && a.buttons == b.buttons && !std::memcmp(a.analog, b.analog, sizeof(a.analog)) &&
           a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
}

void TestSticks() {
    CHECK(Applied({Action::MoveForward}).ly == 32767);
    CHECK(Applied({Action::MoveBack}).ly == -32767);
    CHECK(Applied({Action::MoveLeft}).lx == -32767);
    CHECK(Applied({Action::MoveRight}).lx == 32767);
    CHECK(Applied({Action::LookUp}).ry == 32767);
    CHECK(Applied({Action::LookDown}).ry == -32767);
    CHECK(Applied({Action::LookLeft}).rx == -32767);
    CHECK(Applied({Action::LookRight}).rx == 32767);
}

void TestOppositeKeysCentre() {
    CHECK(Applied({Action::MoveForward, Action::MoveBack}).ly == 0);
    CHECK(Applied({Action::MoveLeft, Action::MoveRight}).lx == 0);
    CHECK(Applied({Action::LookUp, Action::LookDown}).ry == 0);
    CHECK(Applied({Action::LookLeft, Action::LookRight}).rx == 0);

    input::Pad deflected;
    deflected.ly = 20000;
    ApplyKeyboard(Held({Action::MoveForward, Action::MoveBack}), deflected);
    CHECK(deflected.ly == 0);
}

void TestAnalogButtons() {
    constexpr struct {
        Action action;
        int index;
    } kMap[] = {{Action::Jump, 0}, {Action::Attack, 1},      {Action::Punch, 2},         {Action::Kick, 3},
                {Action::Black, 4},     {Action::White, 5},      {Action::LeftTrigger, 6}, {Action::RightTrigger, 7}};
    for (const auto& entry : kMap) {
        input::Pad pad = Applied({entry.action});
        for (int i = 0; i < 8; i++) CHECK(pad.analog[i] == (i == entry.index ? 255 : 0));
        CHECK(pad.buttons == 0);
    }
}

void TestDigitalButtons() {
    constexpr struct {
        Action action;
        uint16_t bit;
    } kMap[] = {{Action::PadUp, 1 << 0},     {Action::PadDown, 1 << 1},  {Action::PadLeft, 1 << 2},      {Action::PadRight, 1 << 3},
                {Action::Start, 1 << 4},  {Action::Back, 1 << 5}, {Action::LeftClick, 1 << 6},   {Action::RightClick, 1 << 7}};
    for (const auto& entry : kMap) {
        input::Pad pad = Applied({entry.action});
        CHECK(pad.buttons == entry.bit);
        for (uint8_t analog : pad.analog) CHECK(analog == 0);
    }
}

void TestKeyboardPlugsIntoAnEmptyPort() {
    CHECK(Applied({}).connected);
    input::Pad zero;
    ApplyKeyboard(KeyState{}, zero);
    CHECK(zero.connected);
    CHECK(zero.buttons == 0);
    CHECK(zero.lx == 0 && zero.ly == 0 && zero.rx == 0 && zero.ry == 0);
}

void TestRealControllerKept() {
    input::Pad real;
    real.connected = true;
    real.buttons = 0x12;
    real.analog[1] = 200;
    real.analog[6] = 90;
    real.lx = -1234;
    real.ly = 4321;
    real.rx = 77;
    real.ry = -77;

    input::Pad kept = real;
    ApplyKeyboard(KeyState{}, kept);
    CHECK(SameReport(kept, real));

    input::Pad both = real;
    ApplyKeyboard(Held({Action::Jump}), both);
    CHECK(both.analog[0] == 255);
    CHECK(both.analog[1] == 200);
    CHECK(both.buttons == 0x12);
    CHECK(both.lx == -1234);
}

void TestKeyMessages() {
    constexpr struct {
        uintptr_t virtual_key;
        Action action;
    } kMap[] = {
        {'W', Action::MoveForward}, {'A', Action::MoveLeft}, {'S', Action::MoveBack}, {'D', Action::MoveRight}, {'I', Action::LookUp}, {'J', Action::LookLeft}, {'K', Action::LookDown},
        {'L', Action::LookRight}, {' ', Action::Jump}, {'C', Action::Attack}, {'E', Action::Punch}, {'Q', Action::Kick}, {'Z', Action::White}, {'V', Action::Black},
        {0x0D, Action::Start}, {0x08, Action::Back}, {0x26, Action::PadUp}, {0x28, Action::PadDown}, {0x25, Action::PadLeft},
        {0x27, Action::PadRight}, {0x09, Action::LeftClick}, {'R', Action::RightClick},
    };
    for (const auto& entry : kMap) {
        KeyState keys;
        UpdateKeyState(keys, kKeyDownMessage, entry.virtual_key, 0);
        CHECK(keys.Has(entry.action));
        UpdateKeyState(keys, kKeyUpMessage, entry.virtual_key, 0);
        CHECK(!keys.Has(entry.action));
    }

    KeyState unmapped;
    UpdateKeyState(unmapped, kKeyDownMessage, 'P', 0);
    CHECK(unmapped.held == 0);
}

void TestOnlyTheLeftModifiersCount() {
    KeyState keys;
    UpdateKeyState(keys, kKeyDownMessage, kVkShift, Scan(0x2A));
    CHECK(keys.Has(Action::LeftTrigger));
    UpdateKeyState(keys, kKeyUpMessage, kVkShift, Scan(0x2A));
    CHECK(!keys.Has(Action::LeftTrigger));
    UpdateKeyState(keys, kKeyDownMessage, kVkShift, Scan(0x36));  // right shift
    CHECK(keys.held == 0);

    UpdateKeyState(keys, kKeyDownMessage, kVkControl, Scan(0x1D));
    CHECK(keys.Has(Action::RightTrigger));
    UpdateKeyState(keys, kKeyUpMessage, kVkControl, Scan(0x1D));
    CHECK(!keys.Has(Action::RightTrigger));
    UpdateKeyState(keys, kKeyDownMessage, kVkControl, Scan(0x1D) | kExtended);  // right control
    CHECK(keys.held == 0);
}

void TestLosingFocusReleasesEveryKey() {
    KeyState keys = Held({Action::MoveForward, Action::Jump, Action::LeftTrigger});
    UpdateKeyState(keys, kKillFocusMessage, 0, 0);
    CHECK(keys.held == 0);
}

}  // namespace

int main() {
    TestSticks();
    TestOppositeKeysCentre();
    TestAnalogButtons();
    TestDigitalButtons();
    TestKeyboardPlugsIntoAnEmptyPort();
    TestRealControllerKept();
    TestKeyMessages();
    TestOnlyTheLeftModifiersCount();
    TestLosingFocusReleasesEveryKey();
    return g_failures;
}
