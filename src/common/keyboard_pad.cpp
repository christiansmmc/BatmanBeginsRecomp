#include "common/keyboard_pad.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

#include "core/input.h"

namespace {

constexpr int16_t kStickFull = 32767;
constexpr uint8_t kAnalogFull = 255;
constexpr uint32_t kRightShiftScanCode = 0x36;
constexpr intptr_t kExtendedKeyBit = intptr_t(1) << 24;

constexpr struct {
    Action action;
    int bit;
} kDigital[] = {
    {Action::PadUp, 0}, {Action::PadDown, 1},  {Action::PadLeft, 2},   {Action::PadRight, 3},
    {Action::Start, 4}, {Action::Back, 5},     {Action::LeftClick, 6}, {Action::RightClick, 7},
};

constexpr struct {
    Action action;
    int index;
} kAnalog[] = {
    {Action::Jump, 0},  {Action::Attack, 1}, {Action::Punch, 2},       {Action::Kick, 3},
    {Action::Black, 4}, {Action::White, 5},  {Action::LeftTrigger, 6}, {Action::RightTrigger, 7},
};

// both keys of an axis held is the centre, neither leaves the controller's value
void ApplyAxis(const KeyState& keys, Action negative, Action positive, int16_t& axis) {
    if (keys.Has(negative) || keys.Has(positive))
        axis = int16_t((keys.Has(positive) ? kStickFull : 0) - (keys.Has(negative) ? kStickFull : 0));
}

std::atomic<uint32_t> g_window_keys{0};
std::atomic<uint32_t> g_window_map[kActionCount];
const bool g_window_map_set = [] {
    for (uint32_t i = 0; i < kActionCount; i++) g_window_map[i].store(kDefaultKeyMap.virtual_key[i], std::memory_order_relaxed);
    return true;
}();

}  // namespace

uint32_t MessageVirtualKey(uintptr_t wp, intptr_t lp) {
    if (wp == VK_SHIFT) return (uint32_t(lp) >> 16 & 0xFF) == kRightShiftScanCode ? kVkRightShift : kVkLeftShift;
    if (wp == VK_CONTROL) return lp & kExtendedKeyBit ? kVkRightControl : kVkLeftControl;
    return uint32_t(wp);
}

void ApplyKeyboard(const KeyState& keys, input::Pad& pad) {
    pad.connected = true;
    ApplyAxis(keys, Action::MoveLeft, Action::MoveRight, pad.lx);
    ApplyAxis(keys, Action::MoveBack, Action::MoveForward, pad.ly);
    ApplyAxis(keys, Action::LookLeft, Action::LookRight, pad.rx);
    ApplyAxis(keys, Action::LookDown, Action::LookUp, pad.ry);
    for (const auto& entry : kDigital)
        if (keys.Has(entry.action)) pad.buttons |= uint16_t(1u << entry.bit);
    for (const auto& entry : kAnalog)
        if (keys.Has(entry.action)) pad.analog[entry.index] = kAnalogFull;
}

void UpdateKeyState(KeyState& keys, uint32_t message, uintptr_t wp, intptr_t lp, const KeyMap& map) {
    if (message == WM_KILLFOCUS) {
        keys.held = 0;
    } else if (message == WM_KEYDOWN || message == WM_KEYUP) {
        const uint32_t key = MessageVirtualKey(wp, lp);
        for (uint32_t i = 0; i < kActionCount; i++)
            if (map.virtual_key[i] == key) keys.Set(Action(i), message == WM_KEYDOWN);
    }
}

KeyMap WindowKeyMap() {
    KeyMap map;
    for (uint32_t i = 0; i < kActionCount; i++) map.virtual_key[i] = g_window_map[i].load(std::memory_order_relaxed);
    return map;
}

void SetWindowKeyMap(const KeyMap& map) {
    for (uint32_t i = 0; i < kActionCount; i++) g_window_map[i].store(map.virtual_key[i], std::memory_order_relaxed);
    g_window_keys.store(0, std::memory_order_relaxed);
}

void KeyboardWindowMessage(uint32_t message, uintptr_t wp, intptr_t lp) {
    KeyState keys{g_window_keys.load(std::memory_order_relaxed)};
    UpdateKeyState(keys, message, wp, lp, WindowKeyMap());
    g_window_keys.store(keys.held, std::memory_order_relaxed);
}

void ApplyWindowKeyboard(int port, input::Pad& pad) {
    if (port == 0) ApplyKeyboard(KeyState{g_window_keys.load(std::memory_order_relaxed)}, pad);
}
