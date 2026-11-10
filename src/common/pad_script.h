// Scripted controller input for the automated runs. The format is the one sdk/harness uses:
// lines of "frame buttons [frames held]", held 8 frames by default, # comments, buttons joined with +.
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace input {
struct Pad;
}

enum PadButton : uint32_t {
    kUp = 1u << 0,
    kDown = 1u << 1,
    kLeft = 1u << 2,
    kRight = 1u << 3,
    kStart = 1u << 4,
    kBack = 1u << 5,
    kA = 1u << 8,
    kB = 1u << 9,
    kX = 1u << 10,
    kY = 1u << 11,
    kLeftTrigger = 1u << 12,
    kRightTrigger = 1u << 13,
    kLeftStick = 1u << 14,  // left stick up
    kLeftStickDown = 1u << 15,
    kLeftStickLeft = 1u << 16,
    kLeftStickRight = 1u << 17,
};

struct PadStep {
    uint32_t frame;
    uint32_t buttons;
    uint32_t held;
};

// throws std::invalid_argument naming the line on anything it cannot read
std::vector<PadStep> ParsePadScript(std::string_view text);
uint32_t ButtonsAtFrame(const std::vector<PadStep>& steps, uint32_t frame);
// adds the buttons to the pad; no buttons leaves it as it was
void ApplyButtons(uint32_t buttons, input::Pad& pad);

// BB_PAD_SCRIPT=<file>: the script for port 0. Throws if it cannot be read. Returns whether one is active.
bool LoadPadScriptFromEnv();
void CountPresentedFrame();
void ApplyPadScript(int port, input::Pad& pad);
