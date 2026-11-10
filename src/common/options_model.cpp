#include "common/options_model.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

#include "core/input.h"

namespace options {

namespace {

constexpr uint32_t kFirstScale = 2, kLastScale = 4;  // the ones validated: docs/TECHNICAL.md, "Options screen"
constexpr uint32_t kWindowPercent = 100;

constexpr uint32_t kVkEscape = 0x1B, kVkF3 = 0x72, kVkLeftWindows = 0x5B, kVkRightWindows = 0x5C;

constexpr uint16_t kStart = 1 << 4, kBack = 1 << 5;
constexpr uint8_t kAnalogHeld = 30;   // a trigger at rest may read a little
constexpr int kStickHeld = 8000;      // nor does a stick rest at exactly 0

// the ones validated; a larger one set by hand in the settings file still works
constexpr WindowSize kWindowSizes[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};

constexpr double kNavFirstDelay = 0.35, kNavNextDelay = 0.12;

std::string Pixels(uint32_t width, uint32_t height) { return std::to_string(width) + " x " + std::to_string(height); }

bool IsWholeScalePercent(uint32_t percent, uint32_t output_height, uint32_t guest_height, uint32_t& scale) {
    for (uint32_t s = kFirstScale; s <= kLastScale; s++) {
        if (RenderPercentFor(s, output_height, guest_height) == percent) {
            scale = s;
            return true;
        }
    }
    return false;
}

bool Reserved(uint32_t virtual_key) {
    return virtual_key == 0 || virtual_key == kVkEscape || virtual_key == kVkF3 || virtual_key == kVkLeftWindows ||
           virtual_key == kVkRightWindows;
}

bool StickHeld(int16_t value) { return std::abs(int(value)) >= kStickHeld; }

}  // namespace

uint32_t RenderPercentFor(uint32_t scale, uint32_t output_height, uint32_t guest_height) {
    if (!scale || !output_height) return kWindowPercent;
    return (scale * guest_height * 100 + output_height - 1) / output_height;
}

std::string ScaleLabel(uint32_t scale, uint32_t guest_width, uint32_t guest_height) {
    return std::to_string(scale) + "x · " + Pixels(scale * guest_width, scale * guest_height);
}

std::vector<ScaleChoice> ScaleChoices(uint32_t current_percent, uint32_t output_height, uint32_t guest_width,
                                      uint32_t guest_height) {
    std::vector<ScaleChoice> choices;
    for (uint32_t s = kFirstScale; s <= kLastScale; s++)
        choices.push_back({RenderPercentFor(s, output_height, guest_height), ScaleLabel(s, guest_width, guest_height)});
    const auto listed = [&](uint32_t percent) {
        return std::any_of(choices.begin(), choices.end(), [&](const ScaleChoice& c) { return c.percent == percent; });
    };
    const auto pixels_at = [&](uint32_t percent) {
        const uint32_t height = output_height * percent / 100;
        return Pixels(height * guest_width / guest_height, height);
    };
    if (!listed(kWindowPercent)) choices.push_back({kWindowPercent, "AUTO · " + pixels_at(kWindowPercent)});
    if (!listed(current_percent))
        choices.push_back({current_percent, std::to_string(current_percent) + "% · " + pixels_at(current_percent)});
    std::sort(choices.begin(), choices.end(), [](const ScaleChoice& a, const ScaleChoice& b) { return a.percent < b.percent; });
    return choices;
}

uint32_t PercentForNewOutput(uint32_t percent, uint32_t old_output_height, uint32_t new_output_height, uint32_t guest_height) {
    if (percent == kWindowPercent) return percent;
    uint32_t scale = 0;
    if (IsWholeScalePercent(percent, old_output_height, guest_height, scale))
        return RenderPercentFor(scale, new_output_height, guest_height);
    return percent;
}

std::vector<WindowSize> WindowSizes(uint32_t desktop_width, uint32_t desktop_height, uint32_t current_width,
                                    uint32_t current_height, bool windowed) {
    std::vector<WindowSize> sizes;
    bool current_listed = !current_width || !current_height;
    for (const WindowSize& size : kWindowSizes) {
        if (size.width > desktop_width || size.height > desktop_height) continue;
        if (windowed && size.width == desktop_width && size.height == desktop_height) continue;
        sizes.push_back(size);
        current_listed = current_listed || (size.width == current_width && size.height == current_height);
    }
    if (!current_listed) {
        sizes.push_back({current_width, current_height});
        std::sort(sizes.begin(), sizes.end(), [](const WindowSize& a, const WindowSize& b) { return a.width * a.height < b.width * b.height; });
    }
    return sizes;
}

bool RebindKey(KeyMap& map, Action action, uint32_t virtual_key) {
    if (Reserved(virtual_key)) return false;
    const uint32_t old_key = map[action];
    for (uint32_t& other : map.virtual_key)
        if (other == virtual_key) other = old_key;
    map[action] = virtual_key;
    return true;
}

bool ValidKeyMap(const KeyMap& map) {
    for (uint32_t i = 0; i < kActionCount; i++) {
        if (Reserved(map.virtual_key[i])) return false;
        for (uint32_t j = i + 1; j < kActionCount; j++)
            if (map.virtual_key[i] == map.virtual_key[j]) return false;
    }
    return true;
}

std::string KeyName(uint32_t virtual_key) {
    static constexpr struct {
        uint32_t virtual_key;
        const char* name;
    } kNames[] = {
        {0x20, "SPACE"},     {0x0D, "ENTER"},      {0x08, "BACKSPACE"}, {0x09, "TAB"},       {0x26, "UP"},
        {0x28, "DOWN"},      {0x25, "LEFT"},       {0x27, "RIGHT"},     {0x2D, "INSERT"},    {0x2E, "DELETE"},
        {0x24, "HOME"},      {0x23, "END"},        {0x21, "PGUP"},      {0x22, "PGDN"},      {0x14, "CAPS"},
        {0x90, "NUM"},       {0x91, "SCROLL"},     {0x13, "PAUSE"},     {0xA0, "LEFT SHIFT"}, {0xA1, "RIGHT SHIFT"},
        {0xA2, "LEFT CTRL"}, {0xA3, "RIGHT CTRL"}, {0xA4, "LEFT ALT"},  {0xA5, "RIGHT ALT"}, {0xBA, "; :"},
        {0xBB, "= +"},     {0xBC, ","},         {0xBD, "- _"},       {0xBE, "."},        {0xBF, "/ ?"},
        {0xC0, "' \""},    {0xC1, "/ ?"},       {0xC2, "NUM ."},     {0xDB, "[ {"},      {0xDC, "\\ |"},
        {0xDD, "] }"},     {0xDE, "~ ^"},
        {0x6A, "NUM *"},   {0x6B, "NUM +"},     {0x6D, "NUM -"},     {0x6E, "NUM ,"},    {0x6F, "NUM /"},
    };
    for (const auto& entry : kNames)
        if (entry.virtual_key == virtual_key) return entry.name;
    if ((virtual_key >= '0' && virtual_key <= '9') || (virtual_key >= 'A' && virtual_key <= 'Z'))
        return std::string(1, char(virtual_key));
    if (virtual_key >= 0x70 && virtual_key <= 0x87) return "F" + std::to_string(virtual_key - 0x70 + 1);
    if (virtual_key >= 0x60 && virtual_key <= 0x69) return "NUM " + std::to_string(virtual_key - 0x60);
    return "KEY " + std::to_string(virtual_key);
}

void PadGate::Apply(bool screen_open, input::Pad& pad) {
    uint8_t analog_down = 0;
    for (int i = 0; i < 8; i++)
        if (pad.analog[i] >= kAnalogHeld) analog_down |= uint8_t(1u << i);
    const int16_t sticks[] = {pad.lx, pad.ly, pad.rx, pad.ry};
    uint8_t sticks_down = 0;
    for (int i = 0; i < 4; i++)
        if (StickHeld(sticks[i])) sticks_down |= uint8_t(1u << i);

    // what is let go stops waiting; what is down now and has to wait starts
    blocked_buttons_ &= pad.buttons;
    blocked_analog_ &= analog_down;
    blocked_sticks_ &= sticks_down;
    if (screen_open || block_next_) {
        blocked_buttons_ |= pad.buttons;
        blocked_analog_ |= analog_down;
        blocked_sticks_ |= sticks_down;
        block_next_ = false;
    }
    if ((pad.buttons & kBack) && (pad.buttons & kStart)) blocked_buttons_ |= kStart;

    if (screen_open) {
        const bool connected = pad.connected;
        pad = input::Pad{};
        pad.connected = connected;
        return;
    }
    pad.buttons &= uint16_t(~blocked_buttons_);
    for (int i = 0; i < 8; i++)
        if (blocked_analog_ >> i & 1) pad.analog[i] = 0;
    if (blocked_sticks_ & 1) pad.lx = 0;
    if (blocked_sticks_ & 2) pad.ly = 0;
    if (blocked_sticks_ & 4) pad.rx = 0;
    if (blocked_sticks_ & 8) pad.ry = 0;
}

bool NavRepeat::Update(bool held, double seconds) {
    if (!held) {
        held_ = false;
        return false;
    }
    if (!held_) {
        held_ = true;
        wait_ = kNavFirstDelay;
        return true;
    }
    wait_ -= seconds;
    if (wait_ > 0.0) return false;
    wait_ += kNavNextDelay;
    return true;
}

}  // namespace options
