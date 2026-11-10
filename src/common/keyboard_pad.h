// The keyboard as player 1. The keys are read from the game window's messages, so they only count while it has focus.
#pragma once
#include <cstdint>

namespace input {
struct Pad;
}

// what a key can do on the controller
enum class Action : uint32_t {
    MoveForward, MoveLeft, MoveBack, MoveRight,  // left stick
    LookUp, LookLeft, LookDown, LookRight,       // right stick
    Jump, Attack, Punch, Kick,                   // A B X Y
    LeftTrigger, RightTrigger,
    White, Black,
    Start, Back,
    PadUp, PadDown, PadLeft, PadRight,           // directional pad
    LeftClick, RightClick,                       // stick clicks
    Count,
};
inline constexpr uint32_t kActionCount = uint32_t(Action::Count);

// which virtual key does each action; left and right modifiers count apart, as VK_LSHIFT and the like
struct KeyMap {
    uint32_t virtual_key[kActionCount];
    uint32_t& operator[](Action action) { return virtual_key[uint32_t(action)]; }
    uint32_t operator[](Action action) const { return virtual_key[uint32_t(action)]; }
};

inline constexpr uint32_t kVkLeftShift = 0xA0, kVkRightShift = 0xA1, kVkLeftControl = 0xA2, kVkRightControl = 0xA3;

inline constexpr KeyMap kDefaultKeyMap = {{
    'W', 'A', 'S', 'D',
    'I', 'J', 'K', 'L',
    0x20 /*space*/, 'C', 'E', 'Q',
    kVkLeftShift, kVkLeftControl,
    'Z', 'V',
    0x0D /*return*/, 0x08 /*backspace*/,
    0x26 /*up*/, 0x28 /*down*/, 0x25 /*left*/, 0x27 /*right*/,
    0x09 /*tab*/, 'R',
}};

struct KeyState {
    uint32_t held = 0;
    bool Has(Action action) const { return held >> uint32_t(action) & 1; }
    void Set(Action action, bool down) {
        const uint32_t bit = 1u << uint32_t(action);
        held = down ? held | bit : held & ~bit;
    }
};

// the key of a window message, with a modifier told apart by its scan code and extended bit
uint32_t MessageVirtualKey(uintptr_t wp, intptr_t lp);

// adds the keys to the report: it is always connected afterwards, and what no key touches stays as the controller had it
void ApplyKeyboard(const KeyState& keys, input::Pad& pad);
// follows a window message: key down, key up, or focus lost, which lets go of everything
void UpdateKeyState(KeyState& keys, uint32_t message, uintptr_t wp, intptr_t lp, const KeyMap& map = kDefaultKeyMap);

// the game window's keyboard: the first from GameInfo::message, the second from GameInfo::pad on port 0
void KeyboardWindowMessage(uint32_t message, uintptr_t wp, intptr_t lp);
void ApplyWindowKeyboard(int port, input::Pad& pad);
// the keys the game window uses; changing them lets go of whatever was held, whose key up would not find its action
KeyMap WindowKeyMap();
void SetWindowKeyMap(const KeyMap& map);
