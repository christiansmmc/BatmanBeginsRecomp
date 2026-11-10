#include "common/pad_script.h"

#include <atomic>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/input.h"

namespace {

constexpr uint32_t kDefaultHeld = 8;
constexpr int16_t kStickFull = 32767;
constexpr uint8_t kAnalogFull = 255;

constexpr std::pair<std::string_view, uint32_t> kNames[] = {
    {"UP", kUp},     {"DOWN", kDown},        {"LEFT", kLeft},          {"RIGHT", kRight},
    {"START", kStart}, {"BACK", kBack},      {"A", kA},                {"B", kB},
    {"X", kX},       {"Y", kY},              {"LT", kLeftTrigger},     {"RT", kRightTrigger},
    {"LS", kLeftStick}, {"LSD", kLeftStickDown}, {"LSL", kLeftStickLeft}, {"LSR", kLeftStickRight},
};

[[noreturn]] void Fail(size_t line, const std::string& why) {
    throw std::invalid_argument("pad script line " + std::to_string(line) + ": " + why);
}

uint32_t ParseNumber(std::string_view text, size_t line) {
    uint32_t value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) Fail(line, "not a number: " + std::string(text));
    return value;
}

uint32_t ParseButtons(std::string_view text, size_t line) {
    uint32_t buttons = 0;
    while (true) {
        size_t plus = text.find('+');
        std::string_view name = text.substr(0, plus);
        bool known = false;
        for (const auto& [candidate, bit] : kNames) {
            if (candidate == name) {
                buttons |= bit;
                known = true;
            }
        }
        if (!known) Fail(line, "unknown button: " + std::string(name));
        if (plus == std::string_view::npos) return buttons;
        text.remove_prefix(plus + 1);
    }
}

std::vector<PadStep> g_script;
std::atomic<uint32_t> g_frame{0};

}  // namespace

std::vector<PadStep> ParsePadScript(std::string_view text) {
    std::vector<PadStep> steps;
    std::istringstream input{std::string(text)};
    std::string raw;
    for (size_t line = 1; std::getline(input, raw); ++line) {
        raw = raw.substr(0, raw.find('#'));
        std::istringstream fields(raw);
        std::string frame, buttons, held;
        if (!(fields >> frame)) continue;
        if (!(fields >> buttons)) Fail(line, "no buttons");
        PadStep step{ParseNumber(frame, line), ParseButtons(buttons, line), kDefaultHeld};
        if (fields >> held) step.held = ParseNumber(held, line);
        steps.push_back(step);
    }
    return steps;
}

uint32_t ButtonsAtFrame(const std::vector<PadStep>& steps, uint32_t frame) {
    uint32_t buttons = 0;
    for (const PadStep& step : steps) {
        if (frame >= step.frame && frame - step.frame < step.held) buttons |= step.buttons;
    }
    return buttons;
}

void ApplyButtons(uint32_t buttons, input::Pad& pad) {
    if (!buttons) return;
    pad.connected = true;
    pad.buttons |= static_cast<uint16_t>(buttons & 0x3F);
    if (buttons & kA) pad.analog[0] = kAnalogFull;
    if (buttons & kB) pad.analog[1] = kAnalogFull;
    if (buttons & kX) pad.analog[2] = kAnalogFull;
    if (buttons & kY) pad.analog[3] = kAnalogFull;
    if (buttons & kLeftTrigger) pad.analog[6] = kAnalogFull;
    if (buttons & kRightTrigger) pad.analog[7] = kAnalogFull;
    if (buttons & kLeftStick) pad.ly = kStickFull;
    if (buttons & kLeftStickDown) pad.ly = -kStickFull;
    if (buttons & kLeftStickLeft) pad.lx = -kStickFull;
    if (buttons & kLeftStickRight) pad.lx = kStickFull;
}

bool LoadPadScriptFromEnv() {
    const char* path = std::getenv("BB_PAD_SCRIPT");
    if (!path || !*path) return false;
    std::ifstream file(path);
    if (!file) throw std::invalid_argument(std::string("cannot read pad script ") + path);
    std::stringstream text;
    text << file.rdbuf();
    g_script = ParsePadScript(text.str());
    return true;
}

void CountPresentedFrame() {
    g_frame.fetch_add(1, std::memory_order_relaxed);
}

void ApplyPadScript(int port, input::Pad& pad) {
    if (port != 0 || g_script.empty()) return;
    pad.connected = true;
    ApplyButtons(ButtonsAtFrame(g_script, g_frame.load(std::memory_order_relaxed)), pad);
}
